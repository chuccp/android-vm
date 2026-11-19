package android.database.sqlite;

import android.database.Cursor;

/**
 * SQL 查询构造器（AOSP android.database.sqlite.SQLiteQueryBuilder 公开面）。
 * 骨架：tables/子句面记录，query 委派 db.query（引擎缝后置：真 SQL 拼装）。
 */
public class SQLiteQueryBuilder {
    private String mTables;

    public SQLiteQueryBuilder() {
    }

    public void setTables(String inTables) {
        mTables = inTables;
    }

    public String getTables() {
        return mTables;
    }

    public Cursor query(SQLiteDatabase db, String[] projectionIn, String selection,
            String[] selectionArgs, String groupBy, String having, String sortOrder) {
        return db.query(mTables, projectionIn, selection, selectionArgs, groupBy, having,
                sortOrder);
    }

    public Cursor query(SQLiteDatabase db, String[] projectionIn, String selection,
            String[] selectionArgs, String groupBy, String having, String sortOrder,
            String limit) {
        return db.query(mTables, projectionIn, selection, selectionArgs, groupBy, having,
                sortOrder, limit);
    }

    public Cursor query(SQLiteDatabase db, String[] projectionIn, String selection,
            String[] selectionArgs, String groupBy, String having, String sortOrder,
            String limit, android.os.CancellationSignal cancellationSignal) {
        return db.query(mTables, projectionIn, selection, selectionArgs, groupBy, having,
                sortOrder, limit);
    }

    public static String buildQueryString(boolean distinct, String tables, String[] columns,
            String where, String groupBy, String having, String orderBy, String limit) {
        StringBuilder sql = new StringBuilder(120);
        sql.append("SELECT ");
        if (distinct) {
            sql.append("DISTINCT ");
        }
        if (columns != null && columns.length != 0) {
            for (int i = 0; i < columns.length; i++) {
                if (i > 0) {
                    sql.append(", ");
                }
                sql.append(columns[i]);
            }
        } else {
            sql.append("* ");
        }
        sql.append(" FROM ").append(tables);
        if (where != null && where.length() > 0) {
            sql.append(" WHERE ").append(where);
        }
        if (groupBy != null && groupBy.length() > 0) {
            sql.append(" GROUP BY ").append(groupBy);
            if (having != null && having.length() > 0) {
                sql.append(" HAVING ").append(having);
            }
        }
        if (orderBy != null && orderBy.length() > 0) {
            sql.append(" ORDER BY ").append(orderBy);
        }
        if (limit != null && limit.length() > 0) {
            sql.append(" LIMIT ").append(limit);
        }
        return sql.toString();
    }

    public void appendWhere(CharSequence inWhere) {
    }

    public void setProjectionMap(java.util.Map<String, String> map) {
    }

    public void setStrict(boolean strict) {
    }
}
