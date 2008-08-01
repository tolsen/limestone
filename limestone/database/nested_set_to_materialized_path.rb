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

  # define array_accum() aggregate function, similar to mysql's GROUP_CONCAT
  dbh.do("CREATE AGGREGATE array_accum( sfunc = array_append, basetype = anyelement, stype = anyarray, initcond = '{}')");

  # insert the materialized paths corresponding to the nested set representation
  dbh.do("INSERT INTO acl_inheritance (SELECT resource_id, array_to_string(array_accum(par_res_id), ',') AS path FROM ( SELECT par_res.resource_id AS par_res_id, chi_res.resource_id AS resource_id, par_res.lft AS par_res_lft FROM dav_acl_inheritance par_res INNER JOIN dav_acl_inheritance chi_res ON par_res.lft <= chi_res.lft AND par_res.rgt >= chi_res.rgt  ORDER BY par_res_lft) inheritance GROUP BY resource_id)");

  exit 0
rescue DBI::DatabaseError => e
  puts "An error occurred"
  puts "Error code: #{e.err}"
  puts "Error message: #{e.errstr}"
ensure
  # disconnect from server
  dbh.disconnect if dbh
end
