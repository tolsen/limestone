#!/usr/bin/ruby -w
require File.dirname(__FILE__) + '/db_connect'


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

db_connect do |dbh|
  # check if bitmarks collection already exists
  id = get_resource_id(dbh, 'd3743422812811de8ff22bcb8b7a3827');
  exit 0 if id != -1

  dbh.do("INSERT INTO resources (uuid, created_at, owner_id, creator_id, type, displayname, contentlanguage, limebar_state) VALUES ('d3743422812811de8ff22bcb8b7a3827', '2009-08-04 18:58:41', 1, 1, 'Collection', 'bitmarks', 'en-US', '')");
  id = get_resource_id(dbh, 'd3743422812811de8ff22bcb8b7a3827');
  dbh.do("INSERT INTO collections (resource_id, auto_version_new_children) VALUES ( #{id}, 5 )");
  dbh.do("INSERT INTO binds(name, collection_id, resource_id, updated_at) VALUES('bitmarks',2,#{id},'2009-08-04 18:58:41')");
  insert_ace(dbh, 'G', id, 1, get_privilege_id(dbh, "all"))
  insert_ace(dbh, 'G', id, 4, get_privilege_id(dbh, "bind-collection"))
  insert_ace(dbh, 'G', id, 3, get_privilege_id(dbh, "read"))
  insert_ace(dbh, 'D', id, 3, get_privilege_id(dbh, "bind"))
  dbh.do("INSERT INTO acl_inheritance (resource_id, path) VALUES (#{id}, '2,#{id}')")
  dbh.do("CREATE RULE set_bitmarks_owner_binds_insert AS ON INSERT TO binds WHERE NEW.collection_id = #{id} DO UPDATE resources SET owner_id = 1 WHERE id = NEW.resource_id")
  dbh.do("CREATE RULE set_bitmarks_owner_binds_update AS ON UPDATE TO binds WHERE NEW.collection_id = #{id} DO UPDATE resources SET owner_id = 1 WHERE id = NEW.resource_id")
end
