package java.util.logging;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * 日志器（JUL 精简语义：层级命名、级别过滤、Handler 发布链）。
 * 根 Logger（""）默认挂 ConsoleHandler（Level.INFO），与 JDK 默认配置等价
 * 到「INFO 及以上输出到 stderr」；无 LogManager 配置面。
 */
public class Logger {

    public static final String GLOBAL_LOGGER_NAME = "global";

    private static final Map<String, Logger> LOGGERS = new HashMap<>();
    private static final Object LOCK = new Object();
    private static final Logger ROOT = new Logger("", null);

    static {
        ROOT.level = Level.INFO;
        ROOT.addHandler(new ConsoleHandler());
    }

    private final String name;
    private final Logger parent;
    private final List<Handler> handlers = new ArrayList<>();
    private Level level;
    private boolean useParentHandlers = true;

    private Logger(String name, Logger parent) {
        this.name = name;
        this.parent = parent;
    }

    public static Logger getLogger(String name) {
        if (name == null) {
            throw new NullPointerException("name");
        }
        synchronized (LOCK) {
            Logger existing = LOGGERS.get(name);
            if (existing != null) {
                return existing;
            }
            Logger nearest = ROOT;
            String ancestor = name;
            int dot = ancestor.lastIndexOf('.');
            while (dot > 0) {
                ancestor = ancestor.substring(0, dot);
                Logger found = LOGGERS.get(ancestor);
                if (found != null) {
                    nearest = found;
                    break;
                }
                dot = ancestor.lastIndexOf('.');
            }
            Logger logger = new Logger(name, nearest);
            LOGGERS.put(name, logger);
            return logger;
        }
    }

    public static Logger getLogger(String name, String resourceBundleName) {
        return getLogger(name);
    }

    public static Logger getAnonymousLogger() {
        return new Logger(null, ROOT);
    }

    public static Logger getAnonymousLogger(String resourceBundleName) {
        return new Logger(null, ROOT);
    }

    public String getName() {
        return name;
    }

    public Level getLevel() {
        return level;
    }

    public void setLevel(Level newLevel) {
        this.level = newLevel;
    }

    public Logger getParent() {
        return parent;
    }

    public void setParent(Logger parent) {
        // 简化：父链在创建时确定（JDK 的 LogManager 面后置）
    }

    public void addHandler(Handler handler) {
        if (handler == null) {
            throw new NullPointerException("handler");
        }
        if (!handlers.contains(handler)) {
            handlers.add(handler);
        }
    }

    public void removeHandler(Handler handler) {
        handlers.remove(handler);
    }

    public Handler[] getHandlers() {
        return handlers.toArray(new Handler[0]);
    }

    public boolean getUseParentHandlers() {
        return useParentHandlers;
    }

    public void setUseParentHandlers(boolean useParentHandlers) {
        this.useParentHandlers = useParentHandlers;
    }

    public boolean isLoggable(Level l) {
        if (l == null) {
            throw new NullPointerException("level");
        }
        if (level != null) {
            return l.intValue() >= level.intValue();
        }
        if (parent != null) {
            return parent.isLoggable(l);
        }
        return l.intValue() >= Level.INFO.intValue();
    }

    public void log(Level l, String msg) {
        log(l, msg, (Object[]) null);
    }

    public void log(Level l, String msg, Object param1) {
        if (!isLoggable(l)) {
            return;
        }
        LogRecord record = new LogRecord(l, msg);
        record.setParameters(new Object[] { param1 });
        doLog(record);
    }

    public void log(Level l, String msg, Object[] params) {
        if (!isLoggable(l)) {
            return;
        }
        LogRecord record = new LogRecord(l, msg);
        record.setParameters(params);
        doLog(record);
    }

    public void log(Level l, String msg, Throwable thrown) {
        if (!isLoggable(l)) {
            return;
        }
        LogRecord record = new LogRecord(l, msg);
        record.setThrown(thrown);
        doLog(record);
    }

    public void log(LogRecord record) {
        if (record == null || record.getLevel() == null || !isLoggable(record.getLevel())) {
            return;
        }
        doLog(record);
    }

    private void doLog(LogRecord record) {
        if (record.getLoggerName() == null && name != null) {
            record.setLoggerName(name);
        }
        for (Handler handler : handlers) {
            handler.publish(record);
        }
        if (useParentHandlers && parent != null) {
            parent.log(record);
        }
    }

    public void severe(String msg) {
        log(Level.SEVERE, msg);
    }

    public void warning(String msg) {
        log(Level.WARNING, msg);
    }

    public void info(String msg) {
        log(Level.INFO, msg);
    }

    public void config(String msg) {
        log(Level.CONFIG, msg);
    }

    public void fine(String msg) {
        log(Level.FINE, msg);
    }

    public void finer(String msg) {
        log(Level.FINER, msg);
    }

    public void finest(String msg) {
        log(Level.FINEST, msg);
    }

    public void entering(String sourceClass, String sourceMethod) {
    }

    public void exiting(String sourceClass, String sourceMethod) {
    }

    public void throwing(String sourceClass, String sourceMethod, Throwable thrown) {
        if (isLoggable(Level.FINER)) {
            LogRecord record = new LogRecord(Level.FINER, "THROW");
            record.setSourceClassName(sourceClass);
            record.setSourceMethodName(sourceMethod);
            record.setThrown(thrown);
            doLog(record);
        }
    }

    @Override
    public String toString() {
        return "Logger[" + name + "]";
    }
}
