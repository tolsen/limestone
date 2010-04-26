#!/usr/bin/ruby -w
require File.dirname(__FILE__) + '/db_connect'

db_connect do |dbh|
  # get the total size of all properties for each owner_id
  rows = dbh.select_all("SELECT owner_id, SUM(octet_length(value)) AS size FROM resources INNER JOIN properties ON properties.resource_id = resources.id GROUP BY owner_id");

  # update quota for each owner_id
  rows.each do |row| dbh.do("UPDATE quota SET used_quota = used_quota + #{row[1]} WHERE principal_id = #{row[0]}") end if rows
end
