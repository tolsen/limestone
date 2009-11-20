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

  # get the folder names for bits that don't have a displayname
  rows = dbh.select_all(
    "SELECT resources.id, b4.name \
        FROM binds b1 \
        INNER JOIN binds b2 ON b2.collection_id = b1.resource_id \
        INNER JOIN binds b3 ON b3.collection_id = b2.resource_id \
        INNER JOIN binds b4 ON b4.collection_id = b3.resource_id \
        INNER JOIN resources ON resources.id = b4.resource_id \
     WHERE b1.collection_id = 2 AND b1.name = 'home' \
        AND b3.name = 'bits' AND resources.displayname IS NULL;");

  # update displayname to bind name
  rows.each do |row| dbh.do("UPDATE resources SET displayname = '#{row[1]}' WHERE id = #{row[0]}") end if rows

  exit 0
rescue DBI::DatabaseError => e
  puts "An error occurred"
  puts "Error code: #{e.err}"
  puts "Error message: #{e.errstr}"
ensure
  # disconnect from server
  dbh.disconnect if dbh
end
