#!/usr/bin/ruby -w
# you need to install libdbi-ruby, libdbd-mysql-ruby and libdbd-pg-ruby debian packages
require 'optparse'
require 'dbi'

@db_hostname = @db_username = @db_password = @db_name = nil

@dbi_dbd_driver = "pg"
@db_hostname = ENV['LIMESTONE_PGSQL_DB_HOST']
@db_port = ENV['LIMESTONE_PGSQL_DB_PORT']
@db_port = 5432 if @db_port.nil?
@db_username = ENV['LIMESTONE_PGSQL_DB_USER']
@db_password = ENV['LIMESTONE_PGSQL_DB_PASS']
@db_name = ENV['LIMESTONE_PGSQL_DB_NAME']

def get_namespace_id(dbh, namespace)
  row = dbh.select_one("SELECT id FROM namespaces WHERE name = '#{namespace}'")
  ns_id = row ? row[0] : -1
end

def insert_namespace(dbh, namespace)
  nrows = dbh.do("INSERT INTO namespaces(name) VALUES ('#{namespace}')")
  raise "Error inserting namespace #{namespace}" unless nrows == 1
end

def insert_acl_privilege(dbh, ns_id, name, abstract, par_id, lft, rgt)
  nrows = dbh.do("INSERT INTO acl_privileges(priv_namespace_id, name, abstract, parent_id, lft, rgt) VALUES (#{ns_id}, '#{name}', 'f', #{par_id}, #{lft}, #{rgt})")
  raise "Error inserting acl_privilege #{name}" unless nrows == 1
end

begin
  # connect to the SQL server
  dbh = DBI.connect("DBI:#{@dbi_dbd_driver}:database=#{@db_name};host=#{@db_hostname};port=#{@db_port}", @db_username, @db_password)

  dav_ns = "DAV:"
  dav_ns_id = get_namespace_id(dbh, dav_ns)
  if dav_ns_id == -1
    insert_namespace(dbh, dav_ns)
    dav_ns_id = get_namespace_id(dbh, dav_ns)
  end
  dbh.do("UPDATE acl_privileges SET priv_namespace_id = #{dav_ns_id} WHERE priv_namespace_id = 0")

  limebits_ns = "http://limebits.com/ns/1.0/"
  ns_id = get_namespace_id(dbh, limebits_ns)
  if ns_id == -1
    insert_namespace(dbh, limebits_ns)
    ns_id = get_namespace_id(dbh, limebits_ns)
  end

  dbh.do("UPDATE acl_privileges SET rgt = 24 WHERE id = 1")
  insert_acl_privilege(dbh, ns_id, "read-private-properties", 'f', 1, 22, 23)
  
  exit 0
rescue DBI::DatabaseError => e
  puts "An error occurred"
  puts "Error code: #{e.err}"
  puts "Error message: #{e.errstr}"
ensure
  # disconnect from server
  dbh.disconnect if dbh
end
