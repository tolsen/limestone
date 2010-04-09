#!/usr/bin/ruby -w

require File.dirname(__FILE__) + '/db_connect'

db_connect do |dbh|
  limebits_ns = "http://limebits.com/ns/1.0/"
  ns_id = get_namespace_id(dbh, limebits_ns)
  if ns_id == -1
    insert_namespace(dbh, limebits_ns)
    ns_id = get_namespace_id(dbh, limebits_ns)
  end

  dbh.do("UPDATE acl_privileges SET rgt = 26 WHERE id = 1")
  dbh.do("UPDATE acl_privileges SET rgt = 23 WHERE id = 7")
  dbh.do("UPDATE acl_privileges SET rgt = 20 WHERE id = 10")
  dbh.do("UPDATE acl_privileges SET lft = 21, rgt = 22 WHERE id = 11")
  dbh.do("UPDATE acl_privileges SET lft = 24, rgt = 25 WHERE id = 12")
  insert_acl_privilege(dbh, ns_id, "bind-collection", 'f', 10, 18, 19)
end

exit 0
