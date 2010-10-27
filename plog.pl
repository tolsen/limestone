#!/usr/bin/perl
# -*-cperl-*-

# This program takes log data from Apache in 'plog1' format on STDIN. The
# logged fields are name value pairs. If the field name is the name of a sql
# column name listed in @fieldnames, then it is inserted into the database.
#
# LogFormat defined in the file:
#  /opt/limebits/src/server-current/limestone/aproot/conf/lime-nonvhost/001-limestone.conf
# ...and documented here:
#  http://httpd.apache.org/docs/trunk/mod/mod_log_config.html
#
# This might look a little cramped at first...
#    Newlines(\n) are the delimeter between fields
#    Colons(:) are the delimeter between name:value pairs
#    The entire log entry is delimeted by an "empty" line of just a newline
#
# LogFormat "field1:value1\nfield2:value2\n....fieldx:valuex\n\n"
#

# This library is provided by the debian package libdbd-pg-perl
use DBD::Pg;
use IO::Handle;
use Getopt::Long;
use Sys::Syslog;

# Statement and database handles
my $dbh;
my $sth;

# Options passed in as arguments, none of them are strictly required. But their use is suggested
# to avoid default action
my ($dbuser, $dbpass, $dbhost, $dbname, $dbtracelog, $dbschema, $debug, $dbport, $ipfilter);

# Default option values
$dbhost = 'localhost';
$dbtable = 'logdata';
$dbtracelog = 'plog_dbtrace.log';
$ipfilter = $ENV{'LIMESTONE_PLOG_FILTERED_IPS'} || '';
@filtered_ips = split(/ /, $ipfilter);

# Collect options
GetOptions(
           'user=s' 	=> \$dbuser, # Database connection user
           'password=s' 	=> \$dbpass, # Database user password
           'host=s' 	=> \$dbhost,     # Database hostname/ip
           'port=i' 	=> \$dbport,     # Database port
           'database=s'	=> \$dbname,     # Database name
           'tracelog=s'	=> \$dbtracelog, # Database trace log used if debugging is on
           'schema=s'	=> \$dbschema,   # Database schema
           'debug=f'	=> \$debug # Presence of this option as argument will enable debugging
          );

# Option processing
if ($dbschema ne "") {
  $dbtable = "$dbschema.$dbtable";
}


# The array and hash are maintained so that
# @fieldnames can maintain a predictable order, necessary for prepared statements
# %validfields can provide an easy lookup table for sql columns expected
# It is expected that the only maintenance that should need to be done to this
# script is to update the @fieldnames array. The order need not match order in
# which Apache spits these out.

my @fieldnames = (
                  'log_version'   , # (int)  3 
                  'http_version'  , # (text) %H
                  'remote_ip'     , # (inet) %h
                  'request_id'    , # (char(24)) %{UNIQUE_ID}e
                  'request_method', # (text) %m 
                  'request_uri'   , # (text) %U
                  'request_referer', # (text) %{Referer}i
                  'virtual_host'  ,  # (text) %{Host}i
                  'destination_uri'      , # (text) %{Destination}i 
                  'received_time' , # (timestamp tz) %{%F %R:%S %z}t
                  'response_microseconds', # (integer) %D
                  'request_firstline'    , # (text) %r
                  'response_status'      , # (smallint) %>s
                  'resource_uuid' , # (bigint) %{LIMESTONE_RESOURCE_UUID}e
                  'response_size' , # (integer) %b 
                  'auth_user'     , # (text) %u
                  'user_agent'    , # (text) %{User-Agent}i
                 );

# Make a map of fieldnames to their index order in the @vals array
$index = 0;
my %validfields = map { $_ => $index++ } @fieldnames;

my @vals = ();

# Entry point for doing stuff... main loop
while (<STDIN>) {
  if (/^$/) {
    filter_out_gratuitous_logs(\@vals) or insert_vals (\@vals);
    @vals =  map { "" } (0..$#fieldnames);
  } else {
    chomp;
    m/^([^:]+):([\s\S]*)$/;
    ($name, $value) = ($1, $2);
	# push only if "valid field"
    if (exists $validfields{$name}) {
      # insert value into correct place in array, index looked up in %validfields
      $vals[$validfields{$name}] = $value;
    }
  }
}


# Subroutines/Functions

sub create_statement_handle {
  if (! $dbh or $dbh->ping < 1) {
    my $cur_time = gmtime;
    print STDERR "${cur_time} plog connecting to database\n";
    $dbh = DBI->connect('dbi:Pg:dbname=' . $dbname . ';host=' . $dbhost . ';port=' . $dbport, $dbuser, $dbpass,{AutoCommit=>1})
      or throw_error($!);

	if ( $debug ) {
      open($tracefile, ">$dbtracelog") or die ($!);
      autoflush $tracefile 1;
      $dbh->pg_server_trace($tracefile);
	}

  }
  if ($error = $dbh->err) {
    throw_error($error);	
  } else {
    $query = 'INSERT INTO ' . $dbtable . ' ("' . join('","', @fieldnames) . '") ' .
      'VALUES (';
    $query .= '?,' x $#fieldnames;
    $query .= '?)';

    $sth = $dbh->prepare($query);
  }
}	

sub insert_vals {

  # Turn dashes into NULL values for numeric columns 
  if (${$_[0]}[$validfields{'response_size'}] eq '-') {
    ${$_[0]}[$validfields{'response_size'}] = undef;
  }

  if (! $sth or $sth->execute( @{$_[0]} ) != 1) {
	# then the insert didn't work right
	# let's give it one more shot then shit the bed

    eval {throw_error($dbh->err);};
    throw_error($@) if $@;

    create_statement_handle();
    if ($sth->execute ( @{$_[0]} ) != 1) {
      #shit the bed
      throw_error($dbh->err);
      die;
    } 
  }
}

sub filter_out_gratuitous_logs {
  # The entry should be filtered out if this function returns true
  my $filter = 0; 
  $vals_ref = $_[0];

  # as per https://trac.limebits.net/ticket/4817
  if ( grep { "$_" eq "${$vals_ref}[$validfields{remote_ip}]" } @filtered_ips ) {
    if (${$vals_ref}[$validfields{request_method}] eq 'OPTIONS' ||
        ${$vals_ref}[$validfields{request_method}] eq 'PROPFIND'
       ) {
      $filter = 1;
    }
  } 	

  $filter;
}

{                               # scope enclosure for myname

  BEGIN {
    my $myname = $0;
  }

  sub throw_error {
	$msg = shift;
	openlog("$myname", 'pid,nofatal', LOG_LOCAL0);
	syslog('NOTICE',"$msg");
	closelog;
  }

}
