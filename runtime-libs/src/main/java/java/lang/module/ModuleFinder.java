package java.lang.module;

import java.nio.file.Path;
import java.security.AccessController;
import java.security.Permission;
import java.security.PrivilegedAction;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;

import jdk.internal.module.ModulePath;
import jdk.internal.module.SystemModuleFinders;

public interface ModuleFinder {

    Optional<ModuleReference> find(String name);

    Set<ModuleReference> findAll();

    static ModuleFinder ofSystem() {
        SecurityManager sm = System.getSecurityManager();
        if (sm != null) {
            sm.checkPermission(new RuntimePermission("accessSystemModules"));
            PrivilegedAction<ModuleFinder> pa = SystemModuleFinders::ofSystem;
            return AccessController.doPrivileged(pa);
        } else {
            return SystemModuleFinders.ofSystem();
        }
    }

    static ModuleFinder of(Path... entries) {

        if (entries.length == 0) {
            return new ModuleFinder() {
                @Override
                public Optional<ModuleReference> find(String name) {
                    Objects.requireNonNull(name);
                    return Optional.empty();
                }

                @Override
                public Set<ModuleReference> findAll() {
                    return Set.of();
                }
            };
        }

        return ModulePath.of(entries);
    }

    static ModuleFinder compose(ModuleFinder... finders) {

        final List<ModuleFinder> finderList = List.of(finders);

        return new ModuleFinder() {
            private final Map<String, ModuleReference> nameToModule = new HashMap<>();
            private Set<ModuleReference> allModules;

            @Override
            public Optional<ModuleReference> find(String name) {

                ModuleReference mref = nameToModule.get(name);
                if (mref != null)
                    return Optional.of(mref);
                Optional<ModuleReference> omref = finderList.stream()
                        .map(f -> f.find(name))
                        .flatMap(Optional::stream)
                        .findFirst();
                omref.ifPresent(m -> nameToModule.put(name, m));
                return omref;
            }

            @Override
            public Set<ModuleReference> findAll() {
                if (allModules != null)
                    return allModules;

                Set<ModuleReference> result = new HashSet<>(nameToModule.values());
                finderList.stream()
                          .flatMap(f -> f.findAll().stream())
                          .forEach(mref -> {
                              String name = mref.descriptor().name();
                              if (nameToModule.putIfAbsent(name, mref) == null) {
                                  result.add(mref);
                              }
                          });
                allModules = Collections.unmodifiableSet(result);
                return allModules;
            }
        };
    }

}
