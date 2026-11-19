package java.lang.module;

import java.io.PrintStream;
import java.lang.module.ModuleDescriptor.Provides;
import java.lang.module.ModuleDescriptor.Requires.Modifier;
import java.net.URI;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Deque;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;

import jdk.internal.module.ModuleHashes;
import jdk.internal.module.ModuleReferenceImpl;
import jdk.internal.module.ModuleTarget;

final class Resolver {

    private final ModuleFinder beforeFinder;
    private final List<Configuration> parents;
    private final ModuleFinder afterFinder;
    private final PrintStream traceOutput;

    private final Map<String, ModuleReference> nameToReference = new HashMap<>();

    private boolean haveAllAutomaticModules;

    private String targetPlatform;

    String targetPlatform() { return targetPlatform; }

    Resolver(ModuleFinder beforeFinder,
             List<Configuration> parents,
             ModuleFinder afterFinder,
             PrintStream traceOutput) {
        this.beforeFinder = beforeFinder;
        this.parents = parents;
        this.afterFinder = afterFinder;
        this.traceOutput = traceOutput;

        for (Configuration parent : parents) {
            String value = parent.targetPlatform();
            if (value != null) {
                if (targetPlatform == null) {
                    targetPlatform = value;
                } else {
                    if (!value.equals(targetPlatform)) {
                        String msg = "Parents have conflicting constraints on target" +
                                     "  platform: " + targetPlatform + ", " + value;
                        throw new IllegalArgumentException(msg);
                    }
                }
            }
        }
    }

    Resolver resolve(Collection<String> roots) {

        Deque<ModuleDescriptor> q = new ArrayDeque<>();
        for (String root : roots) {

            ModuleReference mref = findWithBeforeFinder(root);
            if (mref == null) {

                if (findInParent(root) != null) {

                    continue;
                }

                mref = findWithAfterFinder(root);
                if (mref == null) {
                    findFail("Module %s not found", root);
                }
            }

            if (isTracing()) {
                trace("root %s", nameAndInfo(mref));
            }

            addFoundModule(mref);
            q.push(mref.descriptor());
        }

        resolve(q);

        return this;
    }

    private Set<ModuleDescriptor> resolve(Deque<ModuleDescriptor> q) {
        Set<ModuleDescriptor> resolved = new HashSet<>();

        while (!q.isEmpty()) {
            ModuleDescriptor descriptor = q.poll();
            assert nameToReference.containsKey(descriptor.name());

            if (descriptor.isAutomatic() && !haveAllAutomaticModules) {
                addFoundAutomaticModules().forEach(mref -> {
                    ModuleDescriptor other = mref.descriptor();
                    q.offer(other);
                    if (isTracing()) {
                        trace("%s requires %s", descriptor.name(), nameAndInfo(mref));
                    }
                });
                haveAllAutomaticModules = true;
            }

            for (ModuleDescriptor.Requires requires : descriptor.requires()) {

                if (requires.modifiers().contains(Modifier.STATIC))
                    continue;

                String dn = requires.name();

                ModuleReference mref = findWithBeforeFinder(dn);
                if (mref == null) {

                    if (findInParent(dn) != null) {

                        continue;
                    }

                    mref = findWithAfterFinder(dn);
                    if (mref == null) {
                        findFail("Module %s not found, required by %s",
                                 dn, descriptor.name());
                    }
                }

                if (isTracing() && !dn.equals("java.base")) {
                    trace("%s requires %s", descriptor.name(), nameAndInfo(mref));
                }

                if (!nameToReference.containsKey(dn)) {
                    addFoundModule(mref);
                    q.offer(mref.descriptor());
                }

            }

            resolved.add(descriptor);
        }

        return resolved;
    }

    Resolver bind() {

        Map<String, Set<ModuleReference>> availableProviders = new HashMap<>();
        for (ModuleReference mref : findAll()) {
            ModuleDescriptor descriptor = mref.descriptor();
            if (!descriptor.provides().isEmpty()) {

                for (Provides provides :  descriptor.provides()) {
                    String sn = provides.service();

                    Set<ModuleReference> providers = availableProviders.get(sn);
                    if (providers == null) {
                        providers = new HashSet<>();
                        availableProviders.put(sn, providers);
                    }
                    providers.add(mref);
                }

            }
        }

        Deque<ModuleDescriptor> q = new ArrayDeque<>();

        Set<ModuleDescriptor> initialConsumers;
        if (ModuleLayer.boot() == null) {
            initialConsumers = new HashSet<>();
        } else {
            initialConsumers = parents.stream()
                    .flatMap(Configuration::configurations)
                    .distinct()
                    .flatMap(c -> c.descriptors().stream())
                    .collect(Collectors.toSet());
        }
        for (ModuleReference mref : nameToReference.values()) {
            initialConsumers.add(mref.descriptor());
        }

        Set<ModuleDescriptor> candidateConsumers = initialConsumers;
        do {
            for (ModuleDescriptor descriptor : candidateConsumers) {
                if (!descriptor.uses().isEmpty()) {

                    Set<ModuleDescriptor> modulesToBind = null;
                    if (isTracing()) {
                        modulesToBind = new HashSet<>();
                    }

                    for (String service : descriptor.uses()) {
                        Set<ModuleReference> mrefs = availableProviders.get(service);
                        if (mrefs != null) {
                            for (ModuleReference mref : mrefs) {
                                ModuleDescriptor provider = mref.descriptor();
                                if (!provider.equals(descriptor)) {

                                    if (isTracing() && modulesToBind.add(provider)) {
                                        trace("%s binds %s", descriptor.name(),
                                                nameAndInfo(mref));
                                    }

                                    String pn = provider.name();
                                    if (!nameToReference.containsKey(pn)) {
                                        addFoundModule(mref);
                                        q.push(provider);
                                    }
                                }
                            }
                        }
                    }
                }
            }

            candidateConsumers = resolve(q);
        } while (!candidateConsumers.isEmpty());

        return this;
    }

    private Set<ModuleReference> addFoundAutomaticModules() {
        Set<ModuleReference> result = new HashSet<>();
        findAll().forEach(mref -> {
            String mn = mref.descriptor().name();
            if (mref.descriptor().isAutomatic() && !nameToReference.containsKey(mn)) {
                addFoundModule(mref);
                result.add(mref);
            }
        });
        return result;
    }

    private void addFoundModule(ModuleReference mref) {
        String mn = mref.descriptor().name();

        if (mref instanceof ModuleReferenceImpl) {
            ModuleTarget target = ((ModuleReferenceImpl)mref).moduleTarget();
            if (target != null)
                checkTargetPlatform(mn, target);
        }

        nameToReference.put(mn, mref);
    }

    private void checkTargetPlatform(String mn, ModuleTarget target) {
        String value = target.targetPlatform();
        if (value != null) {
            if (targetPlatform == null) {
                targetPlatform = value;
            } else {
                if (!value.equals(targetPlatform)) {
                    findFail("Module %s has constraints on target platform (%s)"
                             + " that conflict with other modules: %s", mn,
                             value, targetPlatform);
                }
            }
        }
    }

    Map<ResolvedModule, Set<ResolvedModule>> finish(Configuration cf) {
        detectCycles();
        checkHashes();
        Map<ResolvedModule, Set<ResolvedModule>> graph = makeGraph(cf);
        checkExportSuppliers(graph);
        return graph;
    }

    private void detectCycles() {
        visited = new HashSet<>();
        visitPath = new LinkedHashSet<>();
        for (ModuleReference mref : nameToReference.values()) {
            visit(mref.descriptor());
        }
        visited.clear();
    }

    private Set<ModuleDescriptor> visited;

    private Set<ModuleDescriptor> visitPath;

    private void visit(ModuleDescriptor descriptor) {
        if (!visited.contains(descriptor)) {
            boolean added = visitPath.add(descriptor);
            if (!added) {
                resolveFail("Cycle detected: %s", cycleAsString(descriptor));
            }
            for (ModuleDescriptor.Requires requires : descriptor.requires()) {
                String dn = requires.name();

                ModuleReference mref = nameToReference.get(dn);
                if (mref != null) {
                    ModuleDescriptor other = mref.descriptor();
                    if (other != descriptor) {

                        visit(other);
                    }
                }
            }
            visitPath.remove(descriptor);
            visited.add(descriptor);
        }
    }

    private String cycleAsString(ModuleDescriptor descriptor) {
        List<ModuleDescriptor> list = new ArrayList<>(visitPath);
        list.add(descriptor);
        int index = list.indexOf(descriptor);
        return list.stream()
                .skip(index)
                .map(ModuleDescriptor::name)
                .collect(Collectors.joining(" -> "));
    }

    private void checkHashes() {
        for (ModuleReference mref : nameToReference.values()) {

            if (!(mref instanceof ModuleReferenceImpl))
                continue;
            ModuleHashes hashes = ((ModuleReferenceImpl)mref).recordedHashes();
            if (hashes == null)
                continue;

            ModuleDescriptor descriptor = mref.descriptor();
            String algorithm = hashes.algorithm();
            for (String dn : hashes.names()) {
                ModuleReference mref2 = nameToReference.get(dn);
                if (mref2 == null) {
                    ResolvedModule resolvedModule = findInParent(dn);
                    if (resolvedModule != null)
                        mref2 = resolvedModule.reference();
                }
                if (mref2 == null)
                    continue;

                if (!(mref2 instanceof ModuleReferenceImpl)) {
                    findFail("Unable to compute the hash of module %s", dn);
                }

                ModuleReferenceImpl other = (ModuleReferenceImpl)mref2;
                if (other != null) {
                    byte[] recordedHash = hashes.hashFor(dn);
                    byte[] actualHash = other.computeHash(algorithm);
                    if (actualHash == null)
                        findFail("Unable to compute the hash of module %s", dn);
                    if (!Arrays.equals(recordedHash, actualHash)) {
                        findFail("Hash of %s (%s) differs to expected hash (%s)" +
                                 " recorded in %s", dn, toHexString(actualHash),
                                 toHexString(recordedHash), descriptor.name());
                    }
                }
            }

        }
    }

    private static String toHexString(byte[] ba) {
        StringBuilder sb = new StringBuilder(ba.length * 2);
        for (byte b: ba) {
            sb.append(String.format("%02x", b & 0xff));
        }
        return sb.toString();
    }

    private Map<ResolvedModule, Set<ResolvedModule>> makeGraph(Configuration cf) {

        int capacity = 1 + (4 * nameToReference.size())/ 3;

        Map<ResolvedModule, Set<ResolvedModule>> g1 = new HashMap<>(capacity);

        Map<ResolvedModule, Set<ResolvedModule>> g2;

        if (ModuleLayer.boot() == null) {
            g2 = new HashMap<>(capacity);
        } else {
            g2 = parents.stream()
                .flatMap(Configuration::configurations)
                .distinct()
                .flatMap(c ->
                    c.modules().stream().flatMap(m1 ->
                        m1.descriptor().requires().stream()
                            .filter(r -> r.modifiers().contains(Modifier.TRANSITIVE))
                            .flatMap(r -> {
                                Optional<ResolvedModule> m2 = c.findModule(r.name());
                                assert m2.isPresent()
                                        || r.modifiers().contains(Modifier.STATIC);
                                return m2.stream();
                            })
                            .map(m2 -> Map.entry(m1, m2))
                    )
                )

                .collect(Collectors.groupingBy(Map.Entry::getKey,
                        HashMap::new,
                        Collectors.mapping(Map.Entry::getValue, Collectors.toSet())
            ));
        }

        Map<String, ResolvedModule> nameToResolved = new HashMap<>(capacity);

        for (ModuleReference mref : nameToReference.values()) {
            ModuleDescriptor descriptor = mref.descriptor();
            String name = descriptor.name();

            ResolvedModule m1 = computeIfAbsent(nameToResolved, name, cf, mref);

            Set<ResolvedModule> reads = new HashSet<>();
            Set<ResolvedModule> requiresTransitive = new HashSet<>();

            for (ModuleDescriptor.Requires requires : descriptor.requires()) {
                String dn = requires.name();

                ResolvedModule m2 = null;
                ModuleReference mref2 = nameToReference.get(dn);
                if (mref2 != null) {

                    m2 = computeIfAbsent(nameToResolved, dn, cf, mref2);
                } else {

                    m2 = findInParent(dn);
                    if (m2 == null) {
                        assert requires.modifiers().contains(Modifier.STATIC);
                        continue;
                    }
                }

                reads.add(m2);

                if (requires.modifiers().contains(Modifier.TRANSITIVE)) {
                    requiresTransitive.add(m2);
                }

            }

            if (descriptor.isAutomatic()) {

                for (ModuleReference mref2 : nameToReference.values()) {
                    ModuleDescriptor descriptor2 = mref2.descriptor();
                    String name2 = descriptor2.name();

                    if (!name.equals(name2)) {
                        ResolvedModule m2
                            = computeIfAbsent(nameToResolved, name2, cf, mref2);
                        reads.add(m2);
                        if (descriptor2.isAutomatic())
                            requiresTransitive.add(m2);
                    }
                }

                for (Configuration parent : parents) {
                    parent.configurations()
                            .map(Configuration::modules)
                            .flatMap(Set::stream)
                            .forEach(m -> {
                                reads.add(m);
                                if (m.reference().descriptor().isAutomatic())
                                    requiresTransitive.add(m);
                            });
                }
            }

            g1.put(m1, reads);
            g2.put(m1, requiresTransitive);
        }

        boolean changed;
        List<ResolvedModule> toAdd = new ArrayList<>();
        do {
            changed = false;
            for (Set<ResolvedModule> m1Reads : g1.values()) {
                for (ResolvedModule m2 : m1Reads) {
                    Set<ResolvedModule> m2RequiresTransitive = g2.get(m2);
                    if (m2RequiresTransitive != null) {
                        for (ResolvedModule m3 : m2RequiresTransitive) {
                            if (!m1Reads.contains(m3)) {

                                toAdd.add(m3);
                            }
                        }
                    }
                }
                if (!toAdd.isEmpty()) {
                    m1Reads.addAll(toAdd);
                    toAdd.clear();
                    changed = true;
                }
            }
        } while (changed);

        return g1;
    }

    private ResolvedModule computeIfAbsent(Map<String, ResolvedModule> map,
                                           String name,
                                           Configuration cf,
                                           ModuleReference mref)
    {
        ResolvedModule m = map.get(name);
        if (m == null) {
            m = new ResolvedModule(cf, mref);
            map.put(name, m);
        }
        return m;
    }

    private void checkExportSuppliers(Map<ResolvedModule, Set<ResolvedModule>> graph) {

        for (Map.Entry<ResolvedModule, Set<ResolvedModule>> e : graph.entrySet()) {
            ModuleDescriptor descriptor1 = e.getKey().descriptor();
            String name1 = descriptor1.name();

            Set<String> names = new HashSet<>();
            names.add(name1);

            Map<String, ModuleDescriptor> packageToExporter = new HashMap<>();

            Set<String> packages = descriptor1.packages();
            for (String pn : packages) {
                packageToExporter.put(pn, descriptor1);
            }

            Set<ResolvedModule> reads = e.getValue();
            for (ResolvedModule endpoint : reads) {
                ModuleDescriptor descriptor2 = endpoint.descriptor();

                String name2 = descriptor2.name();
                if (descriptor2 != descriptor1 && !names.add(name2)) {
                    if (name2.equals(name1)) {
                        resolveFail("Module %s reads another module named %s",
                                    name1, name1);
                    } else{
                        resolveFail("Module %s reads more than one module named %s",
                                     name1, name2);
                    }
                }

                if (descriptor2.isAutomatic()) {

                    if (descriptor2 != descriptor1) {
                        for (String source : descriptor2.packages()) {
                            ModuleDescriptor supplier
                                = packageToExporter.putIfAbsent(source, descriptor2);

                            if (supplier != null) {
                                failTwoSuppliers(descriptor1, source, descriptor2, supplier);
                            }
                        }

                    }
                } else {
                    for (ModuleDescriptor.Exports export : descriptor2.exports()) {
                        if (export.isQualified()) {
                            if (!export.targets().contains(descriptor1.name()))
                                continue;
                        }

                        String source = export.source();
                        ModuleDescriptor supplier
                            = packageToExporter.putIfAbsent(source, descriptor2);

                        if (supplier != null) {
                            failTwoSuppliers(descriptor1, source, descriptor2, supplier);
                        }
                    }

                }
            }

            if (!descriptor1.isAutomatic()) {

                for (String service : descriptor1.uses()) {
                    String pn = packageName(service);
                    if (!packageToExporter.containsKey(pn)) {
                        resolveFail("Module %s does not read a module that exports %s",
                                    descriptor1.name(), pn);
                    }
                }

                for (ModuleDescriptor.Provides provides : descriptor1.provides()) {
                    String pn = packageName(provides.service());
                    if (!packageToExporter.containsKey(pn)) {
                        resolveFail("Module %s does not read a module that exports %s",
                                    descriptor1.name(), pn);
                    }
                }

            }

        }

    }

    private void failTwoSuppliers(ModuleDescriptor descriptor,
                                  String source,
                                  ModuleDescriptor supplier1,
                                  ModuleDescriptor supplier2) {

        if (supplier2 == descriptor) {
            ModuleDescriptor tmp = supplier1;
            supplier1 = supplier2;
            supplier2 = tmp;
        }

        if (supplier1 == descriptor) {
            resolveFail("Module %s contains package %s"
                         + ", module %s exports package %s to %s",
                    descriptor.name(),
                    source,
                    supplier2.name(),
                    source,
                    descriptor.name());
        } else {
            resolveFail("Modules %s and %s export package %s to module %s",
                    supplier1.name(),
                    supplier2.name(),
                    source,
                    descriptor.name());
        }

    }

    private ResolvedModule findInParent(String mn) {
        for (Configuration parent : parents) {
            Optional<ResolvedModule> om = parent.findModule(mn);
            if (om.isPresent())
                return om.get();
        }
        return null;
    }

    private ModuleReference findWithBeforeFinder(String mn) {

        return beforeFinder.find(mn).orElse(null);

    }

    private ModuleReference findWithAfterFinder(String mn) {
        return afterFinder.find(mn).orElse(null);
    }

    private Set<ModuleReference> findAll() {
        Set<ModuleReference> beforeModules = beforeFinder.findAll();
        Set<ModuleReference> afterModules = afterFinder.findAll();

        if (afterModules.isEmpty())
            return beforeModules;

        if (beforeModules.isEmpty()
                && parents.size() == 1
                && parents.get(0) == Configuration.empty())
            return afterModules;

        Set<ModuleReference> result = new HashSet<>(beforeModules);
        for (ModuleReference mref : afterModules) {
            String name = mref.descriptor().name();
            if (!beforeFinder.find(name).isPresent()
                    && findInParent(name) == null) {
                result.add(mref);
            }
        }

        return result;
    }

    private static String packageName(String cn) {
        int index = cn.lastIndexOf(".");
        return (index == -1) ? "" : cn.substring(0, index);
    }

    private static void findFail(String fmt, Object ... args) {
        String msg = String.format(fmt, args);
        throw new FindException(msg);
    }

    private static void resolveFail(String fmt, Object ... args) {
        String msg = String.format(fmt, args);
        throw new ResolutionException(msg);
    }

    private boolean isTracing() {
        return traceOutput != null;
    }

    private void trace(String fmt, Object ... args) {
        if (traceOutput != null) {
            traceOutput.format(fmt, args);
            traceOutput.println();
        }
    }

    private String nameAndInfo(ModuleReference mref) {
        ModuleDescriptor descriptor = mref.descriptor();
        StringBuilder sb = new StringBuilder(descriptor.name());
        mref.location().ifPresent(uri -> sb.append(" " + uri));
        if (descriptor.isAutomatic())
            sb.append(" automatic");
        return sb.toString();
    }
}
