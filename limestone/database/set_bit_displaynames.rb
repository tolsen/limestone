#!/usr/bin/ruby -w

require File.dirname(__FILE__) + '/db_connect'

db_connect do |dbh|
  # get the folder names for bits that don't have a displayname
  rows = dbh.select_all(
    "SELECT resources.id, b4.name \
        FROM binds b1 \
        INNER JOIN binds b2 ON b2.collection_id = b1.resource_id \
        INNER JOIN binds b3 ON b3.collection_id = b2.resource_id \
        INNER JOIN binds b4 ON b4.collection_id = b3.resource_id \
        INNER JOIN resources ON resources.id = b4.resource_id \
     WHERE b1.collection_id = 2 AND b1.name = 'home' \
        AND b3.name = 'bits' AND resources.displayname = '' OR resources.displayname IS NULL;");

  # update displayname to bind name
  rows.each do |row| dbh.do("UPDATE resources SET displayname = '#{row[1]}' WHERE id = #{row[0]}") end if rows
end

exit 0
