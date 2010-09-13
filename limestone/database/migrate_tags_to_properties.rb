#!/usr/bin/ruby -w

require File.dirname(__FILE__) + '/db_connect'

db_connect do |dbh|
  limebits_ns = "http://limebits.com/ns/1.0/"
  bitmarks_ns = "http://limebits.com/ns/bitmarks/1.0/"

  lb_ns_id = get_namespace_id(dbh, limebits_ns)
  if lb_ns_id == -1
    insert_namespace(dbh, limebits_ns)
    lb_ns_id = get_namespace_id(dbh, limebits_ns)
  end

  bm_ns_id = get_namespace_id(dbh, bitmarks_ns)
  if bm_ns_id != -1
    dbh.do("INSERT INTO properties (namespace_id, name, resource_id, xmlinfo, value) SELECT #{lb_ns_id}, 'tags', resources.id, '<P:tags xmlns:P=\"http://limebits.com/ns/1.0/\">', ',' || concat(bitmarks.value || ',') FROM resources INNER JOIN binds bitmarked_resources ON bitmarked_resources.name = resources.uuid INNER JOIN binds bitmark_resources ON bitmark_resources.collection_id = bitmarked_resources.resource_id INNER JOIN properties bitmarks ON bitmarks.resource_id = bitmark_resources.resource_id AND bitmarks.value != '' AND (bitmarks.namespace_id, bitmarks.name) IN ((#{bm_ns_id}, 'tag')) GROUP BY resources.id")
  end
end
