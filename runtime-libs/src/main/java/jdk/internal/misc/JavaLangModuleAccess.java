package jdk.internal.misc;

// 生成补源（sun-compat）：JavaLangModuleAccess（ModuleDescriptor 构建接线，
// 签名对齐 java.lang.module.ModuleDescriptor 匿名实现）。
import java.io.PrintStream;
import java.lang.module.Configuration;
import java.lang.module.ModuleDescriptor;
import java.lang.module.ModuleFinder;
import java.util.Collection;
import java.util.List;
import java.util.Map;
import java.util.Set;

public interface JavaLangModuleAccess {
    ModuleDescriptor.Builder newModuleBuilder(String mn, boolean strict,
                                              Set<ModuleDescriptor.Modifier> modifiers);
    Set<String> packages(ModuleDescriptor.Builder builder);
    void requires(ModuleDescriptor.Builder builder,
                  Set<ModuleDescriptor.Requires.Modifier> ms, String mn,
                  String rawCompiledVersion);
    ModuleDescriptor.Requires newRequires(Set<ModuleDescriptor.Requires.Modifier> ms,
                                          String mn, ModuleDescriptor.Version v);
    ModuleDescriptor.Exports newExports(Set<ModuleDescriptor.Exports.Modifier> ms,
                                        String source);
    ModuleDescriptor.Exports newExports(Set<ModuleDescriptor.Exports.Modifier> ms,
                                        String source, Set<String> targets);
    ModuleDescriptor.Opens newOpens(Set<ModuleDescriptor.Opens.Modifier> ms,
                                    String source, Set<String> targets);
    ModuleDescriptor.Opens newOpens(Set<ModuleDescriptor.Opens.Modifier> ms,
                                    String source);
    ModuleDescriptor.Provides newProvides(String service, List<String> providers);
    ModuleDescriptor newModuleDescriptor(String name, ModuleDescriptor.Version version,
                                         Set<ModuleDescriptor.Modifier> modifiers,
                                         Set<ModuleDescriptor.Requires> requires,
                                         Set<ModuleDescriptor.Exports> exports,
                                         Set<ModuleDescriptor.Opens> opens,
                                         Set<String> uses,
                                         Set<ModuleDescriptor.Provides> provides,
                                         Set<String> packages, String mainClass,
                                         int hashCode);
    Configuration resolveAndBind(ModuleFinder finder, Collection<String> roots,
                                 PrintStream traceOutput);
    Configuration newConfiguration(ModuleFinder finder,
                                   Map<String, Set<String>> graph);
}
