#!/usr/bin/ruby -w

require File.dirname(__FILE__) + '/db_connect'

db_connect do |dbh|
  # get the lastmodified values we need
  rows = dbh.select_all(
  "WITH RECURSIVE bus(resource_id, updated_at, visited) AS ( \
    SELECT resource_id, updated_at, ARRAY[resource_id]::bigint[] \
        FROM media \
    UNION ALL \
        SELECT collection_id, bus.updated_at, \
          visited::bigint[] || collection_id \
        FROM binds INNER JOIN bus ON binds.resource_id = bus.resource_id \
        WHERE NOT collection_id = ANY(visited)\
   ) SELECT resource_id, MAX(updated_at) from bus GROUP BY resource_id;");

  # update quota for each owner_id
  rows.each do |row| dbh.do("UPDATE resources SET lastmodified = '#{row[1]}' WHERE id = #{row[0]}") end if rows
end

exit 0
