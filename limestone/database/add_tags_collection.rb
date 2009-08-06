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

def get_resource_id(dbh, uuid)
  row = dbh.select_one("SELECT id FROM resources WHERE uuid = '#{uuid}'")
  id = row ? row[0] : -1
end

def get_privilege_id(dbh, privilege)
  row = dbh.select_one("SELECT id FROM acl_privileges WHERE name = '#{privilege}'")
  id = row ? row[0] : -1
end

def insert_ace(dbh, grantdeny, id, principal_id, privilege_id)
  nrows = dbh.do("INSERT INTO aces(grantdeny, resource_id, principal_id, protected) VALUES ('#{grantdeny}', #{id}, #{principal_id}, 't')")
  raise "Error inserting ace" unless nrows == 1

  row = dbh.select_one("SELECT CURRVAL('aces_id_seq')")
  ace_id = row ? row[0] : -1

  nrows = dbh.do("INSERT INTO dav_aces_privileges(ace_id, privilege_id) VALUES (#{ace_id},#{privilege_id})")
  raise "Error inserting ace" unless nrows == 1
end

begin
  # connect to the SQL server
  dbh = DBI.connect("DBI:#{@dbi_dbd_driver}:database=#{@db_name};host=#{@db_hostname};port=#{@db_port}", @db_username, @db_password)

  dbh.do("INSERT INTO resources (uuid, created_at, owner_id, creator_id, type, displayname, contentlanguage, limebar_state) VALUES ('d3743422812811de8ff22bcb8b7a3827', '2009-08-04 18:58:41', 1, 1, 'Collection', 'tags', 'en-US', '')");
  id = get_resource_id(dbh, 'd3743422812811de8ff22bcb8b7a3827');
  dbh.do("INSERT INTO collections (resource_id, auto_version_new_children) VALUES ( #{id}, 5 )");
  dbh.do("INSERT INTO binds(name, collection_id, resource_id, updated_at) VALUES('tags',2,#{id},'2009-08-04 18:58:41')");
  insert_ace(dbh, 'G', id, 1, get_privilege_id(dbh, "all"))
  insert_ace(dbh, 'G', id, 4, get_privilege_id(dbh, "bind-collection"))
  insert_ace(dbh, 'G', id, 3, get_privilege_id(dbh, "read"))
  insert_ace(dbh, 'D', id, 3, get_privilege_id(dbh, "bind"))
  dbh.do("INSERT INTO acl_inheritance (resource_id, path) VALUES (#{id}, '2,#{id}')")
  dbh.do("CREATE RULE set_tags_owner_binds_insert AS ON INSERT TO binds WHERE NEW.collection_id = #{id} DO UPDATE resources SET owner_id = 1 WHERE id = NEW.resource_id")
  dbh.do("CREATE RULE set_tags_owner_binds_update AS ON UPDATE TO binds WHERE NEW.collection_id = #{id} DO UPDATE resources SET owner_id = 1 WHERE id = NEW.resource_id")

  exit 0
rescue DBI::DatabaseError => e
  puts "An error occurred"
  puts "Error code: #{e.err}"
  puts "Error message: #{e.errstr}"
ensure
  # disconnect from server
  dbh.disconnect if dbh
end
