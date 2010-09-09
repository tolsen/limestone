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
    dbh.do("INSERT INTO properties (namespace_id, name, resource_id, xmlinfo, value) 
WITH resource_bitmarks AS(
        SELECT resources.id AS resource_id, bitmarks.value AS value, bitmark_resources.id AS bitmark_resource_id FROM resources INNER JOIN binds bitmarked_resources ON bitmarked_resources.name = resources.uuid INNER JOIN binds bitmark_resources ON bitmark_resources.collection_id = bitmarked_resources.resource_id INNER JOIN properties bitmarks ON bitmarks.resource_id = bitmark_resources.resource_id AND bitmarks.value != '' AND (bitmarks.namespace_id, bitmarks.name) IN ((#{bm_ns_id}, 'description'))
)
SELECT #{lb_ns_id}, 'description', bm.resource_id, '<P:tag xmlns:P=\"http://limebits.com/ns/1.0/\">', bm.value 
    FROM resource_bitmarks bm INNER JOIN ( SELECT resource_id, MAX(bitmark_resource_id) AS bitmark_resource_id FROM resource_bitmarks GROUP BY resource_id ) max_bm ON bm.bitmark_resource_id = max_bm.bitmark_resource_id")
  end
end
