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

  dbh.do("UPDATE resources SET type_id = type_id - 1 WHERE type_id > 7")
  
rescue DBI::DatabaseError => e
  puts "An error occurred"
  puts "Error code: #{e.err}"
  puts "Error message: #{e.errstr}"
ensure
  # disconnect from server
  dbh.disconnect if dbh
end
