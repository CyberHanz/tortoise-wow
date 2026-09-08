for f in sql/database_updates/*.sql; do
  n=$(basename "$f" .sql)
  mysql -u mangos -pmangos -e "INSERT IGNORE INTO tw_world.migrations (Name, Hash, AppliedAt) VALUES ('$n','manual',NOW());"
done
