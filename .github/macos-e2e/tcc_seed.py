#!/usr/bin/env python3
import sqlite3
import sys
import time


def default_value(name, declared_type, not_null):
    if name in {"service", "client", "indirect_object_identifier"}:
        return "UNUSED"
    if name in {"auth_value", "allowed"}:
        return 0
    if name in {"auth_reason", "prompt_count"}:
        return 4
    if name == "auth_version":
        return 1
    if name == "last_modified":
        return int(time.time())
    if name in {"client_type", "indirect_object_identifier_type", "flags"}:
        return 0
    if not not_null:
        return None
    return "" if "CHAR" in declared_type or "TEXT" in declared_type else 0


def main():
    if len(sys.argv) not in {5, 7}:
        raise SystemExit("usage: tcc_seed.py DB SERVICE CLIENT AUTH [INDIRECT TYPE]")
    db, service, client, auth = sys.argv[1:5]
    indirect = sys.argv[5] if len(sys.argv) == 7 else None
    client_type = int(sys.argv[6]) if len(sys.argv) == 7 else 0
    connection = sqlite3.connect(db, timeout=10)
    columns = connection.execute("pragma table_info(access)").fetchall()
    if not columns:
        raise RuntimeError(f"no access table in {db}")
    names = [column[1] for column in columns]
    template = connection.execute(
        "select * from access where service=? limit 1", (service,)
    ).fetchone()
    if template is None:
        template = connection.execute("select * from access limit 1").fetchone()
    values = dict(zip(names, template)) if template else {
        column[1]: default_value(column[1], column[2].upper(), column[3])
        for column in columns
    }
    values.update(service=service, client=client, client_type=client_type)
    if "auth_value" in values:
        values["auth_value"] = int(auth)
    if "allowed" in values:
        values["allowed"] = 1 if int(auth) == 2 else 0
    if "auth_reason" in values:
        values["auth_reason"] = 4
    if "auth_version" in values:
        values["auth_version"] = 1
    if "last_modified" in values:
        values["last_modified"] = int(time.time())
    if "flags" in values:
        values["flags"] = 0
    if "csreq" in values:
        values["csreq"] = None
    if indirect is not None and "indirect_object_identifier" in values:
        values["indirect_object_identifier"] = indirect
        values["indirect_object_identifier_type"] = 0
    where = "service=? and client=? and client_type=?"
    args = [service, client, client_type]
    if indirect is not None and "indirect_object_identifier" in names:
        where += " and indirect_object_identifier=?"
        args.append(indirect)
    connection.execute(f"delete from access where {where}", args)
    placeholders = ",".join("?" for _ in names)
    quoted = ",".join(f'"{name}"' for name in names)
    connection.execute(
        f"insert into access ({quoted}) values ({placeholders})",
        [values[name] for name in names],
    )
    connection.commit()
    row = connection.execute(f"select * from access where {where}", args).fetchone()
    print(dict(zip(names, row)))


if __name__ == "__main__":
    main()
