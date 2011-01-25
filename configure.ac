# ====================================================================
# Copyright 2011 Lime Labs LLC
# Copyright 2007 Lime Spot LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# ====================================================================

dnl Process this file with autoconf to produce a configure script.


AC_INIT(LimeStone, patsubst(esyscmd([./limestone/version -n]), ['\n'], []))

dnl Define the default database here.
WITH_DB_FLAG_PROVIDED="no"
INSTALL_CONF_FLAGS=""
VHOSTS=""
PGSQL_JDBC_JAR=""
DROP_AND_CREATE=limestone/drop_and_create_populated_database
APACHE_CONF_FLAGS=""
ENABLE_PROFILE="no"

rm -f $DROP_AND_CREATE

AC_PROG_AWK
AC_PROG_SED
AC_PROG_GREP

dnl check for --with-pgsql
AC_MSG_CHECKING([for --with-pgsql])
AC_ARG_WITH(pgsql,
            [AS_HELP_STRING([--with-pgsql=PATH], 
                [Specify PostgreSQL location])],
            [
                AC_MSG_RESULT([using PostgreSQL database])
                WITH_DB_FLAG_PROVIDED="yes"
                
                dnl set db flag
                APACHE_CONF_FLAGS=$APACHE_CONF_FLAGS"--with-pgsql=$withval "
                PGSQL_VHOST="yes"
                VHOSTS=$VHOSTS" PGSQL"

				PG_PATH=$withval
				# check for pg_config 
				AC_PATH_PROG([PG_CONFIG], [pg_config], [], [$PG_PATH:$PG_PATH/bin:$PATH])
				if test "$PG_CONFIG" = ""; then
				    AC_MSG_ERROR([Cannot locate pg_config])
				fi
                
				# check for PostgreSQL version
				AC_MSG_CHECKING([for PostgreSQL version])
				PG_VERSION=$($PG_CONFIG --version|$AWK '{ print $2 }')
				PG_MAJOR_VERSION=$(echo "$PG_VERSION"|$AWK -F. '{ print $1 }')
				PG_MINOR_VERSION=$(echo "$PG_VERSION"|$AWK -F. '{ print substr($2,1,1) }')
				AC_MSG_RESULT([$PG_VERSION])
                
				STOP="no"
				if test $PG_MAJOR_VERSION -lt 8 ; then
				    STOP="yes"
				else
				    if test "$PG_MAJOR_VERSION" = 8 -a "$PG_MINOR_VERSION" -lt 3 ; then
					STOP="yes"
				    fi
				fi
                
                if test "$STOP" = "yes"; then                           
				    AC_MSG_ERROR([failed dependency PostgreSQL >= 8.3])
				fi


                INSTALL_CONF_FLAGS=$INSTALL_CONF_FLAGS" pgsql"

                cat ${DROP_AND_CREATE}_pgsql >> $DROP_AND_CREATE
                
                DROP_AND_CREATE_ACTUAL=${DROP_AND_CREATE_LIN}_pgsql

                
                dnl update .pgpass
                dnl check if the appropriate entry already exists, not handling hostname/port for now.
                if ! $GREP -q -s $LIMESTONE_PGSQL_DB_NAME:$LIMESTONE_PGSQL_DB_USER:$LIMESTONE_PGSQL_DB_PASS$ ~/.pgpass && ! $GREP -q -s "*:$LIMESTONE_PGSQL_DB_USER:$LIMESTONE_PGSQL_DB_PASS" ~/.pgpass; then 
                    echo "*:*:$LIMESTONE_PGSQL_DB_NAME:$LIMESTONE_PGSQL_DB_USER:$LIMESTONE_PGSQL_DB_PASS" >> ~/.pgpass
                    chmod 600 ~/.pgpass
                fi
            ],
            AC_MSG_RESULT(no))

dnl check for --with-mysql
AC_MSG_CHECKING([for --with-mysql])
AC_ARG_WITH(mysql,
            [AS_HELP_STRING([--with-mysql=PATH], [Specify MySQL location])],
            [
                AC_MSG_RESULT([using MySQL database])
                WITH_DB_FLAG_PROVIDED="yes"
            
                dnl set db flag
                APACHE_CONF_FLAGS=$APACHE_CONF_FLAGS"--with-mysql=$withval " 
                VHOSTS=$VHOSTS" MYSQL"

                INSTALL_CONF_FLAGS=$INSTALL_CONF_FLAGS" mysql"
                cat ${DROP_AND_CREATE}_mysql >> $DROP_AND_CREATE

            ],
            AC_MSG_RESULT(no))

chmod +x $DROP_AND_CREATE
            
dnl check that db flag is provided
if test "$WITH_DB_FLAG_PROVIDED" = "no"
then
    AC_MSG_ERROR([atleast one database location must be provided using --with-* flags])
fi

AC_MSG_CHECKING([for --enable-profile])
AC_ARG_ENABLE(profile,
              [AS_HELP_STRING([--enable-profile], [enabling static profile build])],
              [
                  AC_MSG_RESULT([static profile build enabled])
                  APACHE_CONF_FLAGS=$APACHE_CONF_FLAGS" --enable-profile --with-mpm=prefork"
                  ENABLE_PROFILE="yes"
              ],
              [
                  AC_MSG_RESULT(no)
                  APACHE_CONF_FLAGS=$APACHE_CONF_FLAGS" --enable-so --with-mpm=worker"
              ]
             )
        
if test "$PGSQL_VHOST" = "yes"
then
        AC_MSG_CHECKING([for --with-pgsql-jdbc-jar])
        AC_ARG_WITH(pgsql-jdbc-jar,
                [AS_HELP_STRING([--with-pgsql-jdbc-jar=PATH],
                        [Specify location of postgresql jdbc jar file])],
                [
                        AC_MSG_RESULT([using postgresql jar file $withval])
                        PGSQL_JDBC_JAR=$withval
                ],
                [
                        AC_MSG_RESULT(no)
                        PGSQL_JDBC_JAR="/usr/share/java/postgresql.jar"
                        AC_MSG_RESULT(will attempt to use $PGSQL_JDBC_JAR)
                ])
        if ! test -f "$PGSQL_JDBC_JAR"; then
                AC_MSG_ERROR([$PGSQL_JDBC_JAR file doesn't exist])
        fi
fi

AC_MSG_CHECKING([whether to use authbind])
AUTHBIND=""
AUTHBIND_PATH=`which authbind`
if (test x${LIMESTONE_PGSQL_VHOST_PORT} = x || test ${LIMESTONE_PGSQL_VHOST_PORT} -lt 1024) && \
   test x${AUTHBIND_PATH} != x; then
   AC_MSG_RESULT(yes)
   AUTHBIND="authbind --deep"
else
   AC_MSG_RESULT(no)
fi

AC_MSG_CHECKING([whether to use sudo])
SUDO_COMMAND=""
if test x${SUDO_USER} != x
then
   AC_MSG_RESULT(yes)
   SUDO_COMMAND="sudo -E -u ${SUDO_USER}"
else
   AC_MSG_RESULT(no)
fi
    
  

dnl Output variables
AC_SUBST(INSTALL_CONF_FLAGS)
AC_SUBST(VHOSTS)
AC_SUBST(PGSQL_JDBC_JAR)
AC_SUBST(APACHE_CONF_FLAGS)
AC_SUBST(ENABLE_PROFILE)
AC_SUBST(AUTHBIND)
AC_SUBST(SUDO_COMMAND)

dnl Output files
AC_OUTPUT(Makefile)
