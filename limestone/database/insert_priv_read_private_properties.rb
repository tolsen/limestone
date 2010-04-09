#!/usr/bin/ruby -w

require File.dirname(__FILE__) + '/db_connect'

db_connect do |dbh|
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
end

exit 0
