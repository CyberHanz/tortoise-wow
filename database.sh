for f in sql/database_updates/*.sql; do mysql --force -umangos -pmangos tw_world < "$f"; done
