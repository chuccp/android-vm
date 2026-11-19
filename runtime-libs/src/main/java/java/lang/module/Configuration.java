package java.lang.module;

import java.io.PrintStream;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.Deque;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import java.util.stream.Stream;

import jdk.internal.misc.VM;
import jdk.internal.module.ModuleReferenceImpl;
import jdk.internal.module.ModuleTarget;
import jdk.internal.vm.annotation.Stable;

public final class Configuration {

    private static @Stable Configuration EMPTY_CONFIGURATION;

    static {

        VM.initializeFromArchive(Configuration.class);

        if (EMPTY_CONFIGURATION == null) {
            EMPTY_CONFIGURATION = new Configuration();
        }
    }

    private final List<Configuration> parents;

    private final Map<ResolvedModule, Set<ResolvedModule>> graph;
    private final Set<ResolvedModule> modules;
    private final Map<String, ResolvedModule> nameToModule;

    private final String targetPlatform;

    String targetPlatform() { return targetPlatform; }

    private Configuration() {
        this.parents = List.of();
        this.graph = Map.of();
        this.modules = Set.of();
        this.nameToModule = Map.of();
        this.targetPlatform = null;
    }

    private Configuration(List<Configuration> parents, Resolver resolver) {
        Map<ResolvedModule, Set<ResolvedModule>> g = resolver.finish(this);

        @SuppressWarnings(value = {"rawtypes", "unchecked"})
        Entry<String, ResolvedModule>[] nameEntries
            = (Entry<String, ResolvedModule>[])new Entry[g.size()];
        ResolvedModule[] moduleArray = new ResolvedModule[g.size()];
        int i = 0;
        for (ResolvedModule resolvedModule : g.keySet()) {
            moduleArray[i] = resolvedModule;
            nameEntries[i] = Map.entry(resolvedModule.name(), resolvedModule);
            i++;
        }

        this.parents = List.copyOf(parents);
        this.graph = g;
        this.modules = Set.of(moduleArray);
        this.nameToModule = Map.ofEntries(nameEntries);

        this.targetPlatform = resolver.targetPlatform();
    }

    Configuration(ModuleFinder finder, Map<String, Set<String>> map) {
        int moduleCount = map.size();

        @SuppressWarnings(value = {"rawtypes", "unchecked"})
        Entry<String, ResolvedModule>[] nameEntries
            = (Entry<String, ResolvedModule>[])new Entry[moduleCount];
        ResolvedModule[] moduleArray = new ResolvedModule[moduleCount];
        String targetPlatform = null;
        int i = 0;
        for (String name : map.keySet()) {
            ModuleReference mref = finder.find(name).orElse(null);
            assert mref != null;

            if (targetPlatform == null && mref instanceof ModuleReferenceImpl) {
                ModuleTarget target = ((ModuleReferenceImpl)mref).moduleTarget();
                if (target != null) {
                    targetPlatform = target.targetPlatform();
                }
            }

            ResolvedModule resolvedModule = new ResolvedModule(this, mref);
            moduleArray[i] = resolvedModule;
            nameEntries[i] = Map.entry(name, resolvedModule);
            i++;
        }
        Map<String, ResolvedModule> nameToModule = Map.ofEntries(nameEntries);

        @SuppressWarnings(value = {"rawtypes", "unchecked"})
        Entry<ResolvedModule, Set<ResolvedModule>>[] moduleEntries
            = (Entry<ResolvedModule, Set<ResolvedModule>>[])new Entry[moduleCount];
        i = 0;
        for (ResolvedModule resolvedModule : moduleArray) {
            Set<String> names = map.get(resolvedModule.name());
            ResolvedModule[] readsArray = new ResolvedModule[names.size()];
            int j = 0;
            for (String name : names) {
                readsArray[j++] = nameToModule.get(name);
            }
            moduleEntries[i++] = Map.entry(resolvedModule, Set.of(readsArray));
        }

        this.parents = List.of(empty());
        this.graph = Map.ofEntries(moduleEntries);
        this.modules = Set.of(moduleArray);
        this.nameToModule = nameToModule;
        this.targetPlatform = targetPlatform;
    }

    public Configuration resolve(ModuleFinder before,
                                 ModuleFinder after,
                                 Collection<String> roots)
    {
        return resolve(before, List.of(this), after, roots);
    }

    public Configuration resolveAndBind(ModuleFinder before,
                                        ModuleFinder after,
                                        Collection<String> roots)
    {
        return resolveAndBind(before, List.of(this), after, roots);
    }

    static Configuration resolveAndBind(ModuleFinder finder,
                                        Collection<String> roots,
                                        PrintStream traceOutput)
    {
        List<Configuration> parents = List.of(empty());
        Resolver resolver = new Resolver(finder, parents, ModuleFinder.of(), traceOutput);
        resolver.resolve(roots).bind();
        return new Configuration(parents, resolver);
    }

    public static Configuration resolve(ModuleFinder before,
                                        List<Configuration> parents,
                                        ModuleFinder after,
                                        Collection<String> roots)
    {
        Objects.requireNonNull(before);
        Objects.requireNonNull(after);
        Objects.requireNonNull(roots);

        List<Configuration> parentList = new ArrayList<>(parents);
        if (parentList.isEmpty())
            throw new IllegalArgumentException("'parents' is empty");

        Resolver resolver = new Resolver(before, parentList, after, null);
        resolver.resolve(roots);

        return new Configuration(parentList, resolver);
    }

    public static Configuration resolveAndBind(ModuleFinder before,
                                               List<Configuration> parents,
                                               ModuleFinder after,
                                               Collection<String> roots)
    {
        Objects.requireNonNull(before);
        Objects.requireNonNull(after);
        Objects.requireNonNull(roots);

        List<Configuration> parentList = new ArrayList<>(parents);
        if (parentList.isEmpty())
            throw new IllegalArgumentException("'parents' is empty");

        Resolver resolver = new Resolver(before, parentList, after, null);
        resolver.resolve(roots).bind();

        return new Configuration(parentList, resolver);
    }

    public static Configuration empty() {
        return EMPTY_CONFIGURATION;
    }

    public List<Configuration> parents() {
        return parents;
    }

    public Set<ResolvedModule> modules() {
        return modules;
    }

    public Optional<ResolvedModule> findModule(String name) {
        Objects.requireNonNull(name);
        ResolvedModule m = nameToModule.get(name);
        if (m != null)
            return Optional.of(m);

        if (!parents.isEmpty()) {
            return configurations()
                    .skip(1)
                    .map(cf -> cf.nameToModule.get(name))
                    .filter(Objects::nonNull)
                    .findFirst();
        }

        return Optional.empty();
    }

    Set<ModuleDescriptor> descriptors() {
        if (modules.isEmpty()) {
            return Set.of();
        } else {
            return modules.stream()
                    .map(ResolvedModule::reference)
                    .map(ModuleReference::descriptor)
                    .collect(Collectors.toSet());
        }
    }

    Set<ResolvedModule> reads(ResolvedModule m) {
        return Collections.unmodifiableSet(graph.get(m));
    }

    Stream<Configuration> configurations() {
        List<Configuration> allConfigurations = this.allConfigurations;
        if (allConfigurations == null) {
            allConfigurations = new ArrayList<>();
            Set<Configuration> visited = new HashSet<>();
            Deque<Configuration> stack = new ArrayDeque<>();
            visited.add(this);
            stack.push(this);
            while (!stack.isEmpty()) {
                Configuration layer = stack.pop();
                allConfigurations.add(layer);

                for (int i = layer.parents.size() - 1; i >= 0; i--) {
                    Configuration parent = layer.parents.get(i);
                    if (!visited.contains(parent)) {
                        visited.add(parent);
                        stack.push(parent);
                    }
                }
            }
            this.allConfigurations = allConfigurations;
        }
        return allConfigurations.stream();
    }

    private volatile List<Configuration> allConfigurations;

    @Override
    public String toString() {
        return modules().stream()
                .map(ResolvedModule::name)
                .collect(Collectors.joining(", "));
    }
}
