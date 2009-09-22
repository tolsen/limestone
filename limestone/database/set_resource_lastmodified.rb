#!/usr/bin/ruby -w
# you need to install libdbi-ruby, libdbd-mysql-ruby and libdbd-pg-ruby debian packages
require 'dbi'

@db_hostname = @db_username = @db_password = @db_name = nil

@dbi_dbd_driver = "pg"
@db_hostname = ENV['LIMESTONE_PGSQL_DB_HOST']
@db_port = ENV['LIMESTONE_PGSQL_DB_PORT']
@db_port = 5432 if @db_port.nil?
@db_username = ENV['LIMESTONE_PGSQL_DB_USER']
@db_password = ENV['LIMESTONE_PGSQL_DB_PASS']
@db_name = ENV['LIMESTONE_PGSQL_DB_NAME']

begin
  # connect to the SQL server
  dbh = DBI.connect("DBI:#{@dbi_dbd_driver}:database=#{@db_name};host=#{@db_hostname};port=#{@db_port}", @db_username, @db_password)

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

  exit 0
rescue DBI::DatabaseError => e
  puts "An error occurred"
  puts "Error code: #{e.err}"
  puts "Error message: #{e.errstr}"
ensure
  # disconnect from server
  dbh.disconnect if dbh
end
