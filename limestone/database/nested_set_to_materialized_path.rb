#!/usr/bin/ruby -w

require File.dirname(__FILE__) + '/db_connect'

db_connect do |dbh|
  # define array_accum() aggregate function, similar to mysql's GROUP_CONCAT
  dbh.do("CREATE AGGREGATE array_accum( sfunc = array_append, basetype = anyelement, stype = anyarray, initcond = '{}')");

  # insert the materialized paths corresponding to the nested set representation
  dbh.do("INSERT INTO acl_inheritance (SELECT resource_id, array_to_string(array_accum(par_res_id), ',') AS path FROM ( SELECT par_res.resource_id AS par_res_id, chi_res.resource_id AS resource_id, par_res.lft AS par_res_lft FROM dav_acl_inheritance par_res INNER JOIN dav_acl_inheritance chi_res ON par_res.lft <= chi_res.lft AND par_res.rgt >= chi_res.rgt  ORDER BY par_res_lft) inheritance GROUP BY resource_id)");
end

exit 0
