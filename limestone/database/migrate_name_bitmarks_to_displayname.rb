#!/usr/bin/ruby -w

require File.dirname(__FILE__) + '/db_connect'

db_connect do |dbh|
  # get the 'name' bitmarks we need
  rows = dbh.select_all(
    "SELECT resources.id, properties.value\
         FROM properties\
         INNER JOIN binds bitmark_resources\
             ON bitmark_resources.resource_id = properties.resource_id\
         INNER JOIN binds bitmark_collections\
             ON bitmark_collections.resource_id = bitmark_resources.collection_id\
         INNER JOIN resources \
             ON resources.uuid = bitmark_collections.name\
     WHERE properties.name = 'name';");

  # update displayname for these resources
  rows.each do |row| dbh.do("UPDATE resources SET displayname = '#{row[1]}' WHERE id = #{row[0]}") end if rows
end

exit 0
