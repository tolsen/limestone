#!/usr/bin/ruby -w
require File.dirname(__FILE__) + '/db_connect'

db_connect do |dbh|
  dbh.do("CREATE TYPE resourcetype AS ENUM ('Resource', 'Collection', 'Principal', 'User', 'Group', 'Redirect', 'Version', 'VersionControlled', 'VersionHistory', 'VersionedCollection', 'CollectionVersion', 'LockNull')")
end
