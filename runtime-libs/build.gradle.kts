import java.security.MessageDigest
import java.util.Properties
import javax.inject.Inject
import groovy.json.JsonSlurper
import org.gradle.api.DefaultTask
import org.gradle.api.file.ConfigurableFileCollection
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.file.FileCollection
import org.gradle.api.tasks.InputDirectory
import org.gradle.api.file.RegularFileProperty
import org.gradle.api.tasks.OutputFile
import org.gradle.api.provider.ListProperty
import org.gradle.api.provider.MapProperty
import org.gradle.api.provider.Property
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.InputFiles
import org.gradle.api.tasks.OutputDirectory
import org.gradle.api.tasks.TaskAction
import org.gradle.jvm.tasks.Jar
import org.gradle.process.ExecOperations
import java.util.Locale
import javax.tools.DiagnosticCollector
import javax.tools.JavaFileObject
import javax.tools.ToolProvider

plugins {
    base
}

group = "com.coke.vm.runtime-libs"
version = "1.0.0"

val bootLibraries = configurations.create("bootLibraries")

dependencies {
    bootLibraries("com.google.re2j:re2j:1.8")
    bootLibraries("org.json:json:20240303")
}

val sdkDirectory = rootProject.file("local.properties")
    .takeIf { it.isFile }
    ?.let { file ->
        Properties().apply {
            file.inputStream().use(::load)
        }.getProperty("sdk.dir")
    }
    ?: error("sdk.dir is missing from local.properties")
val runtimeAndroidJar = sdkDirectory
    .let(::file)
    .resolve("platforms")
    .listFiles()
    .orEmpty()
    .filter(File::isDirectory)
    .sortedByDescending(File::getName)
    .asSequence()
    .map { it.resolve("android.jar") }
    .firstOrNull(File::isFile)
    ?: error("Android platform android.jar was not found")

val runtimeSourceDirectory = layout.projectDirectory.dir("src/main/java")
val runtimeClasses = layout.buildDirectory.dir("runtime-classes")
val runtimeAndroidClasses = layout.buildDirectory.dir("runtime-android-classes")

// ---- 单一事实源：core/builtin_classes.def（X 宏内建类清单）----
// 废除 allowlist（runtime-components runtimeClasses）/ excluded 表单
// （builtin-excluded.txt / jre-excluded.txt）后，内建类集唯一权威表达。
// 打包排除集由此自动派生（前缀规则 name / name$… 覆盖内部类）；C++ 注册面
// （core/builtins.cc、core/framework_builtins.cc）与 tools/runtime_coverage_tests.sh
// 同源读取（「内建注册集 == 派生集」一致性断言，防再犯核心）。
val builtinClassesDef =
    rootProject.file("app/src/main/cpp/core/builtin_classes.def")

object BuiltinClassManifest {
    private val entry = Regex("""^DEXVM_(?:CORE|FRAMEWORK)_BUILTIN_CLASS\("([^"]+)"\)""")

    // 描述符 "Lx/y/Z;" -> 点号名 x.y.Z（内部类 $ 保留）
    fun readClasses(file: File): List<String> {
        val out = mutableListOf<String>()
        file.readLines(Charsets.UTF_8).forEach { raw ->
            val m = entry.find(raw.trim()) ?: return@forEach
            val desc = m.groupValues[1]
            check(desc.startsWith("L") && desc.endsWith(";")) {
                "Malformed descriptor in builtin_classes.def: $desc"
            }
            out.add(desc.substring(1, desc.length - 1).replace('/', '.'))
        }
        check(out.isNotEmpty()) {
            "builtin_classes.def has no manifest entries: $file"
        }
        check(out.distinct().size == out.size) {
            "builtin_classes.def has duplicate entries: " +
                out.groupingBy { it }.eachCount().filterValues { it > 1 }.keys.joinToString()
        }
        return out
    }
}

// 打包排除集（自动派生，无手工清单）：内建独占类不入 runtime.dex（builtin
// 全类遮蔽 dex 同名类，防双实现漂移，docs 03 章 8.4 / 14 章 §31.4）。
val derivedExclusionClasses: List<String> =
    BuiltinClassManifest.readClasses(builtinClassesDef)

// ---- runtime-components.json：纯元数据（组件/版本/类清单/外部依赖记录）----
// 打包集合不再由此决定（allowlist 已废除）；externalClasses 仅用于
// filteredBootLibrariesJar 的第三方 jar 提取口径。
val runtimeComponentsFile =
    layout.projectDirectory.file("runtime-components.json")
val runtimeComponentsText =
    providers.fileContents(runtimeComponentsFile).asText

fun readStringList(component: Map<*, *>, name: String): List<String> =
    (component[name] as? List<*>)
        .orEmpty()
        .map { it.toString() }

@Suppress("UNCHECKED_CAST")
fun readRuntimeComponents(text: String): List<Map<*, *>> {
    val document = JsonSlurper().parseText(text) as Map<*, *>
    check(document["schemaVersion"] == 2) {
        "Unsupported runtime component schema (expect metadata-only v2)"
    }
    return (document["components"] as? List<*>)
        .orEmpty()
        .map { it as Map<*, *> }
}

// vendored 源码 → 顶层类型清单（一个 .java 可声明多个顶层类；package-info 以
// 文件名计）。className -> 相对 src/main/java 的源路径。仅用于内建 source
// override 契约校验（清单内有源者必须产出 override 类或被自动跳过并记录）。
object VendoredSources {
    fun topLevelTypes(text: String): List<String> {
        val out = mutableListOf<String>()
        var i = 0
        var depth = 0
        while (i < text.length) {
            val c = text[i]
            if (c == '/' && i + 1 < text.length && text[i + 1] == '/') {
                val j = text.indexOf('\n', i)
                i = if (j < 0) text.length else j
                continue
            }
            if (c == '/' && i + 1 < text.length && text[i + 1] == '*') {
                val j = text.indexOf("*/", i + 2)
                i = if (j < 0) text.length else j + 2
                continue
            }
            if (c == '"' || c == '\'') {
                val q = c
                i++
                while (i < text.length && text[i] != q) {
                    if (text[i] == '\\') i++
                    i++
                }
                i++
                continue
            }
            if (c == '{') { depth++; i++; continue }
            if (c == '}') { depth = maxOf(0, depth - 1); i++; continue }
            if (depth == 0) {
                val rest = text.substring(i)
                val m = Regex("^(?:@interface|class|interface|enum)\\s+([A-Za-z_$][\\w$]*)")
                    .find(rest)
                if (m != null && (i == 0 || !(text[i - 1].isLetterOrDigit() ||
                            text[i - 1] == '_' || text[i - 1] == '$' || text[i - 1] == '.'))
                ) {
                    out.add(m.groupValues[1])
                    i += m.range.last + 1
                    continue
                }
            }
            i++
        }
        return out
    }

    fun classToSource(root: File): Map<String, String> {
        val map = mutableMapOf<String, String>()
        root.walkTopDown().filter { it.isFile && it.extension == "java" }.forEach { file ->
            val rel = file.relativeTo(root).invariantSeparatorsPath
            val base = rel.removeSuffix(".java").replace('/', '.')
            val pkg = if (base.contains('.')) base.substringBeforeLast('.') else ""
            val names = topLevelTypes(file.readText(Charsets.UTF_8))
            val declared = (listOf(base.substringAfterLast('.')) + names).distinct()
            declared.forEach { name ->
                val fqcn = if (pkg.isEmpty()) name else "$pkg.$name"
                map[fqcn] = rel
            }
        }
        return map
    }
}

val vendoredClassSources: Map<String, String> =
    VendoredSources.classToSource(runtimeSourceDirectory.asFile)

val runtimeComponentSpecs = readRuntimeComponents(
    runtimeComponentsText.get(),
)
val externalPackagePrefixes = runtimeComponentSpecs.flatMap {
    readStringList(it, "externalClasses")
}.distinct()

fun classFilePatterns(prefixes: Iterable<String>): List<String> =
    prefixes.flatMap { prefix ->
        val path = prefix.replace('.', '/')
        listOf("$path.class", "$path\$*.class")
    }

fun pathPatterns(prefixes: Iterable<String>): List<String> =
    prefixes.map { it.replace('.', '/') + "/**" }

// §31.4 排除集过滤（belt-and-braces：派生集前缀规则覆盖内部类，防误打包）。
val activeExclusionPatterns: List<String> =
    classFilePatterns(derivedExclusionClasses)

val runtimeCompileClasspath: FileCollection =
    bootLibraries + files(runtimeAndroidJar)

// ---- 全量 vendored 源编译 + 剔除集构建期自动跳过（无手工清单）----
// 打包 = 全量 vendored 编译产物 − 自动派生排除集（builtin_classes.def）−
// 自动跳过的编不过源（本任务按 javac 错误归属迭代剔除并记录
// build/runtime-packaging/uncompilable.txt；旧行为等价于人工 81 类剔除清单，
// 差异仅来自「剔除集自动化」：个别旧剔除类现可编译并入包，见 report）。
abstract class CompileRuntimeClassesTask : DefaultTask() {
    @get:InputDirectory
    abstract val sourceDirectory: DirectoryProperty

    @get:InputFiles
    abstract val classpath: ConfigurableFileCollection

    @get:OutputDirectory
    abstract val classesDirectory: DirectoryProperty

    @get:OutputDirectory
    abstract val androidClassesDirectory: DirectoryProperty

    @get:OutputFile
    abstract val uncompilableRecord: RegularFileProperty

    @TaskAction
    fun compile() {
        val sourceRoot = sourceDirectory.get().asFile
        val javaOut = classesDirectory.get().asFile
        val androidOut = androidClassesDirectory.get().asFile
        javaOut.deleteRecursively()
        androidOut.deleteRecursively()
        javaOut.mkdirs()
        androidOut.mkdirs()

        // 全量暂存（java 桶 = java.base patch 源；android 桶独立编译）。
        // 注：--patch-module 的 patch 目录内源码全量参与编译，剔除必须从
        // 暂存目录物理删除（错误归属迭代）。
        val stage = temporaryDir
        val javaRoot = stage.resolve("java-src")
        val androidRoot = stage.resolve("android-src")
        javaRoot.deleteRecursively()
        androidRoot.deleteRecursively()
        javaRoot.mkdirs()
        androidRoot.mkdirs()
        var staged = 0
        sourceRoot.walkTopDown().filter { it.isFile && it.extension == "java" }.forEach { file ->
            val rel = file.relativeTo(sourceRoot).invariantSeparatorsPath
            val destRoot = if (rel.startsWith("android/")) androidRoot else javaRoot
            val dest = destRoot.resolve(rel)
            dest.parentFile.mkdirs()
            file.copyTo(dest, overwrite = true)
            staged++
        }

        val classpathString = classpath.files
            .filter(File::isFile)
            .sortedBy(File::getName)
            .joinToString(File.pathSeparator) { it.absolutePath }
        check(classpathString.isNotEmpty()) {
            "Runtime compile classpath is empty"
        }

        val skipped = mutableListOf<String>()

        // java 桶（--patch-module java.base，source override 语义）
        compileBucket(
            root = javaRoot,
            outDir = javaOut,
            options = listOf(
                "-g",
                "-source", "11",
                "-target", "11",
                "-Xmaxerrs", "10000",
                "-nowarn",
                "-encoding", "UTF-8",
                "--patch-module", "java.base=${javaRoot.absolutePath}",
                "--add-reads", "java.base=ALL-UNNAMED",
                "-classpath", classpathString,
                "-d", javaOut.absolutePath,
            ),
            skipped = skipped,
        )
        // android 桶（对 android.jar 编译，不入 patch module）
        compileBucket(
            root = androidRoot,
            outDir = androidOut,
            options = listOf(
                "-g",
                "-source", "11",
                "-target", "11",
                "-Xmaxerrs", "10000",
                "-nowarn",
                "-encoding", "UTF-8",
                "-classpath", classpathString,
                "-d", androidOut.absolutePath,
            ),
            skipped = skipped,
        )

        // 剔除集记录（构建期自动跳过并记录，不再人工清单）
        val record = uncompilableRecord.get().asFile
        record.parentFile.mkdirs()
        record.writeText(
            buildString {
                appendLine("# uncompilable vendored sources (auto-derived at build time;")
                appendLine("# skipped from compilation and recorded here; no manual list)")
                skipped.sorted().forEach { appendLine(it) }
            },
            Charsets.UTF_8,
        )
        val javaCount = javaOut.walkTopDown().count { it.isFile && it.extension == "class" }
        val androidCount = androidOut.walkTopDown().count { it.isFile && it.extension == "class" }
        logger.lifecycle(
            "runtime compile: staged=$staged skipped=${skipped.size} " +
                "classes=${javaCount + androidCount} (java=$javaCount android=$androidCount)",
        )
    }

    private fun compileBucket(
        root: File,
        outDir: File,
        options: List<String>,
        skipped: MutableList<String>,
    ) {
        val compiler = ToolProvider.getSystemJavaCompiler()
            ?: error("No system Java compiler (JDK required)")
        val maxRounds = 50
        repeat(maxRounds) { round ->
            val sources = root.walkTopDown()
                .filter { it.isFile && it.extension == "java" }
                .sortedBy { it.invariantSeparatorsPath }
                .toList()
            if (sources.isEmpty()) return
            val diagnostics = DiagnosticCollector<JavaFileObject>()
            compiler.getStandardFileManager(diagnostics, null, Charsets.UTF_8).use { fileManager ->
                val units = fileManager.getJavaFileObjectsFromFiles(sources)
                val ok = compiler.getTask(
                    null,
                    fileManager,
                    diagnostics,
                    options,
                    null,
                    units,
                ).call()
                if (ok) return
                // 错误归属：带 ERROR 诊断的源文件整体剔除，迭代重编译至收敛
                val failing = diagnostics.diagnostics
                    .filter {
                        it.kind == javax.tools.Diagnostic.Kind.ERROR &&
                            it.source != null
                    }
                    .mapNotNull { it.source }
                    .map { File(it.toUri()).canonicalFile }
                    .filter { it.path.startsWith(root.canonicalPath) }
                    .toSet()
                if (failing.isEmpty()) {
                    val text = diagnostics.diagnostics.joinToString("\n") {
                        "${it.kind} ${it.source?.name.orEmpty()}:${it.lineNumber}: " +
                            it.getMessage(Locale.ROOT)
                    }
                    error(
                        "Runtime compile failed with no attributable sources " +
                            "(round $round):\n$text",
                    )
                }
                failing.forEach { file ->
                    file.delete()
                    skipped.add(file.relativeTo(root).invariantSeparatorsPath)
                }
                logger.lifecycle(
                    "runtime compile round $round: dropping ${failing.size} " +
                        "uncompilable source(s)",
                )
            }
        }
        error("Runtime compile did not converge in $maxRounds rounds ($root)")
    }
}

val compileRuntimeClasses =
    tasks.register<CompileRuntimeClassesTask>("compileRuntimeClasses") {
        sourceDirectory.set(runtimeSourceDirectory)
        classpath.from(runtimeCompileClasspath)
        classesDirectory.set(runtimeClasses)
        androidClassesDirectory.set(runtimeAndroidClasses)
        uncompilableRecord.set(
            layout.buildDirectory.file("runtime-packaging/uncompilable.txt"),
        )
    }

// ---- 校验（allowlist∩excluded=∅ 已随表单废除；新口径）----
//   派生集 ∩ 打包集 = ∅（天然成立的校验，belt-and-braces）
//   编译产物 = 打包集 ⊎ 派生集覆盖（内建 source override）
//   内建 source override 契约：清单内有 vendored 源者，必须产出 override 类
//   或被自动跳过并记录（uncompilable.txt）。
abstract class VerifyRuntimePackagingTask : DefaultTask() {
    @get:InputDirectory
    abstract val classesDirectory: DirectoryProperty

    @get:InputDirectory
    abstract val androidClassesDirectory: DirectoryProperty

    @get:InputFile
    abstract val manifestFile: RegularFileProperty

    @get:InputFile
    abstract val uncompilableRecord: RegularFileProperty

    @get:Input
    abstract val vendoredSources: MapProperty<String, String>

    @get:OutputFile
    abstract val reportFile: RegularFileProperty

    @TaskAction
    fun verify() {
        val derived = BuiltinClassManifest.readClasses(manifestFile.get().asFile)
        val skippedSources = uncompilableRecord.get().asFile.readLines(Charsets.UTF_8)
            .map(String::trim)
            .filter { it.isNotEmpty() && !it.startsWith("#") }
            .toSet()
        val compiled = listOf(
            classesDirectory.get().asFile,
            androidClassesDirectory.get().asFile,
        ).flatMap { root ->
            root.walkTopDown()
                .filter { it.isFile && it.extension == "class" }
                .map {
                    it.relativeTo(root).invariantSeparatorsPath
                        .removeSuffix(".class")
                        .replace('/', '.')
                }
                .toList()
        }
        val covered = { className: String ->
            derived.any { prefix ->
                className == prefix || className.startsWith("$prefix$")
            }
        }
        val packaged = compiled.filterNot(covered).sorted()
        val overrides = compiled.filter(covered).sorted()
        // 派生集 ∩ 打包集 = ∅（天然成立；显式断言防回归）
        val overlap = packaged.filter(covered)
        check(overlap.isEmpty()) {
            "derived exclusion set overlaps packaged set: " +
                overlap.joinToString()
        }
        check(overrides.isNotEmpty()) {
            "no builtin source-override classes were compiled " +
                "(derived set expected to compile as overrides)"
        }
        // 内建 source override 契约：有源者必须产出 override 或被自动跳过记录
        val broken = derived.filter { name ->
            vendoredSources.get().containsKey(name) &&
                !compiled.any { it == name || it.startsWith("$name$") } &&
                vendoredSources.get()[name] !in skippedSources
        }
        check(broken.isEmpty()) {
            "builtin classes with vendored source neither compiled as " +
                "override nor recorded as auto-skipped: " + broken.sorted().joinToString()
        }
        reportFile.get().asFile.parentFile.mkdirs()
        reportFile.get().asFile.writeText(
            buildString {
                appendLine("policy=derived-exclusion")
                appendLine("compiled=${compiled.size}")
                appendLine("packaged=${packaged.size}")
                appendLine("excluded=${derived.size}")
                appendLine("sourceOverrides=${overrides.size}")
                appendLine("autoSkippedSources=${skippedSources.size}")
                appendLine("packagedClasses=" + packaged.joinToString())
                appendLine("excludedClasses=" + derived.sorted().joinToString())
                appendLine("sourceOverrideClasses=" + overrides.joinToString())
                appendLine("autoSkippedSourceFiles=" + skippedSources.sorted().joinToString())
            },
            Charsets.UTF_8,
        )
    }
}

val verifyRuntimePackaging =
    tasks.register<VerifyRuntimePackagingTask>("verifyRuntimePackaging") {
        dependsOn(compileRuntimeClasses)
        classesDirectory.set(runtimeClasses)
        androidClassesDirectory.set(runtimeAndroidClasses)
        manifestFile.set(builtinClassesDef)
        uncompilableRecord.set(
            layout.buildDirectory.file("runtime-packaging/uncompilable.txt"),
        )
        vendoredSources.set(vendoredClassSources)
        reportFile.set(
            layout.buildDirectory.file("runtime-packaging/report.txt"),
        )
    }

val runtimeClassesJar = tasks.register<Jar>("runtimeClassesJar") {
    dependsOn(verifyRuntimePackaging)
    archiveFileName.set("runtime-libs.jar")
    destinationDirectory.set(layout.buildDirectory.dir("runtime-jar"))
    from(runtimeClasses) {
        exclude(activeExclusionPatterns)
    }
    from(runtimeAndroidClasses) {
        exclude(activeExclusionPatterns)
    }
}

val filteredBootLibrariesJar =
    tasks.register<Jar>("filteredBootLibrariesJar") {
        archiveFileName.set("runtime-boot-libraries.jar")
        destinationDirectory.set(
            layout.buildDirectory.dir("runtime-jar"),
        )
        from({
            bootLibraries.files.map { zipTree(it) }
        }) {
            include(pathPatterns(externalPackagePrefixes))
            exclude("META-INF/**", "module-info.class")
        }
    }

abstract class DexRuntimeLibraryTask : DefaultTask() {
    @get:InputFiles
    abstract val classpath: ConfigurableFileCollection

    @get:Input
    abstract val androidSdkDirectory: Property<String>

    @get:Input
    abstract val bundleVersion: Property<String>

    @get:InputFile
    abstract val componentsFile: RegularFileProperty

    @get:Input
    abstract val derivedExclusions: ListProperty<String>

    @get:InputFile
    abstract val uncompilableRecord: RegularFileProperty

    @get:OutputDirectory
    abstract val outputDirectory: DirectoryProperty

    @get:Inject
    abstract val execOperations: ExecOperations

    @TaskAction
    fun dex() {
        @Suppress("UNCHECKED_CAST")
        val componentDocument = JsonSlurper().parse(
            componentsFile.get().asFile,
        ) as Map<*, *>
        val componentSpecs =
            (componentDocument["components"] as? List<*>)
                .orEmpty()
                .map { it as Map<*, *> }
        // 派生排除集（builtin_classes.def）：内建独占类不得被 jar/dex 打包。
        val enforcedExcluded = derivedExclusions.get().sorted()
        val externalPrefixes = componentSpecs.flatMap { component ->
            (component["externalClasses"] as? List<*>)
                .orEmpty()
                .map { it.toString() }
        }
        val excludedClassEntries = enforcedExcluded.flatMap {
            prefix ->
            val path = prefix.replace('.', '/')
            listOf("$path.class", "$path\$")
        }
        val packagedEntries = classpath.files
            .filter { it.isFile && it.extension == "jar" }
            .flatMap { jar ->
                java.util.zip.ZipFile(jar).use { archive ->
                    val packagedExcluded =
                        archive.entries().asSequence()
                            .map { it.name }
                            .filter { entry ->
                                excludedClassEntries.any {
                                    exclusion ->
                                    entry == exclusion ||
                                        entry.startsWith(exclusion)
                                }
                            }
                            .toList()
                    check(packagedExcluded.isEmpty()) {
                        "Derived-exclusion classes were packaged into ${jar.name}: " +
                            packagedExcluded.joinToString()
                    }
                    archive.entries().asSequence()
                        .map { it.name }
                        .toList()
                }
            }
        externalPrefixes.forEach { prefix ->
            val path = prefix.replace('.', '/') + "/"
            check(packagedEntries.any { it.startsWith(path) }) {
                "External runtime classes are missing: $prefix"
            }
        }
        val sdk = File(androidSdkDirectory.get())
        val d8 = sdk.resolve("build-tools")
            .listFiles()
            .orEmpty()
            .filter(File::isDirectory)
            .sortedByDescending(File::getName)
            .asSequence()
            .flatMap { directory ->
                listOf(
                    directory.resolve("d8"),
                    directory.resolve("d8.bat"),
                ).asSequence()
            }
            .firstOrNull(File::isFile)
            ?: error("Android d8 executable was not found")
        val androidJar = sdk.resolve("platforms")
            .listFiles()
            .orEmpty()
            .filter(File::isDirectory)
            .sortedByDescending(File::getName)
            .asSequence()
            .map { it.resolve("android.jar") }
            .firstOrNull(File::isFile)
            ?: error("Android platform android.jar was not found")
        val output = outputDirectory.get().asFile
        output.deleteRecursively()
        check(output.mkdirs() || output.isDirectory) {
            "Unable to create runtime library DEX directory"
        }
        execOperations.exec {
            val inputs = classpath.files
                .filter(File::isFile)
                .sortedBy(File::getName)
                .map(File::getAbsolutePath)
            check(inputs.isNotEmpty()) {
                "Runtime library has no input JARs"
            }
            commandLine(
                listOf(
                    d8.absolutePath,
                    // min-api 26 → dex 038：invoke-polymorphic（VarHandle accessor /
                    // MethodHandle marker 形）以真 opcode 保留。min-api 24（dex 037）
                    // 下 d8 把这些调用点整体替换成
                    // "Instruction is unrepresentable in DEX V37: invoke-polymorphic"
                    // 抛出桩——juc VarHandle 面全灭。lambda 仍由 d8 脱糖为合成类
                    // （实测 min-api 26 不出 invoke-custom），无回归。
                    "--min-api",
                    "26",
                    "--lib",
                    androidJar.absolutePath,
                    "--output",
                    output.absolutePath,
                ) + inputs,
            )
        }
        val classesDexFiles = output.listFiles()
            .orEmpty()
            .filter {
                it.isFile &&
                    it.name.matches(Regex("""classes(?:[2-9][0-9]*)?\.dex"""))
            }
            .sortedWith(
                compareBy {
                    it.name.removePrefix("classes")
                        .removeSuffix(".dex")
                        .ifBlank { "1" }
                        .toInt()
                },
            )
        check(classesDexFiles.isNotEmpty()) {
            "d8 did not produce runtime library DEX files"
        }
        val dexDescriptors = classesDexFiles.mapIndexed { index, classesDex ->
            val runtimeDex = output.resolve(
                if (index == 0) "runtime.dex" else "runtime${index + 1}.dex",
            )
            check(classesDex.renameTo(runtimeDex)) {
                "Unable to name runtime library DEX ${runtimeDex.name}"
            }
            mapOf(
                "name" to runtimeDex.name,
                "sha256" to MessageDigest.getInstance("SHA-256")
                    .digest(runtimeDex.readBytes())
                    .joinToString("") {
                        byte -> "%02x".format(byte.toInt() and 0xff)
                    },
            )
        }
        val components = componentSpecs.map { component ->
            mapOf(
                "id" to component["id"].toString(),
                "version" to component["version"].toString(),
                "classes" to
                    (component["classes"] as? List<*>)
                        .orEmpty()
                        .map { it.toString() },
            )
        }
        val skippedSources = uncompilableRecord.get().asFile.readLines(Charsets.UTF_8)
            .map(String::trim)
            .filter { it.isNotEmpty() && !it.startsWith("#") }
            .sorted()
        val manifest = mapOf(
            "schemaVersion" to 2,
            "bundle" to mapOf(
                "id" to "runtime-libs",
                "version" to bundleVersion.get(),
                "dexFiles" to dexDescriptors,
                "components" to components,
                "packaging" to mapOf(
                    "policy" to "derived-exclusion",
                    "source" to "core/builtin_classes.def",
                    "excludedClasses" to enforcedExcluded,
                    "autoSkippedSources" to skippedSources,
                ),
            ),
        )
        output.resolve("manifest.json").writeText(
            groovy.json.JsonOutput.prettyPrint(
                groovy.json.JsonOutput.toJson(manifest),
            ) + "\n",
        )
    }
}

val dexRuntimeSdk = tasks.register<DexRuntimeLibraryTask>("dexRuntimeSdk") {
    dependsOn(runtimeClassesJar, filteredBootLibrariesJar)
    classpath.from(runtimeClassesJar.flatMap { it.archiveFile })
    classpath.from(
        filteredBootLibrariesJar.flatMap { it.archiveFile },
    )
    androidSdkDirectory.set(sdkDirectory)
    bundleVersion.set(project.version.toString())
    componentsFile.set(runtimeComponentsFile)
    derivedExclusions.set(derivedExclusionClasses)
    uncompilableRecord.set(
        layout.buildDirectory.file("runtime-packaging/uncompilable.txt"),
    )
    outputDirectory.set(layout.buildDirectory.dir("runtime-dex"))
}

tasks.named("build") {
    dependsOn(dexRuntimeSdk)
}
