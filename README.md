BaseX-FUSE
==========
This is a basic and unoptimized FUSE filesystem implementation for accessing a BaseX XML database as a filesystem -- for example, to edit an xml document in BaseX with your favorite text editor. It supports basic filesystem operations.

Notably, this does not implement any kind of permissions and leaves deciding whether one ought to write or read a file to the BaseX backend. 

I do not recommend using this in production or with any data that you haven't backed up as this is something I hacked 
together to ease personal use. It has worked fine for me, but *caveat usor.*

Compilation
----

1. Clone the repo
2. `sudo dnf install fuse fuse-devel libubsan openssl-devel`
3. `make` -- and install any other dependencies it complains about
4. optionally, `make install` to put the binary in your path

Use
----
You will need to have basex running on a server. By default, it expects the default port of 1984. 

The following environment variables exist to control connection parameters:

| Variable | Description                           | Default Value |
---------------------
| DBHOST   | The hostname of the database          | localhost     |
| DBPORT   | The port the database is listening on | 1984          |
| DBUSER   | Database user to login as             | admin         |
| DBPASSWD | Database password for login           | test          |
| DBNAME   | Which database to mount               | default       |

Having set the appropriate values, you can run `./basexfuse target` where *target* is the directory you wish to mount
your database to.

Limitations
-----
Limitations exist due to the structure of the Linux file API and BaseX's C API. 
Likely it would be better to build this on top of BaseX's java which has much better access to document metadata. 

Notably, this implementation caches documents to memory aggressively as it otherwise needs to make many more
round-trips to the database in order to please the filesystem API. 
This may lead to weirdness like the cache getting out of sync with the database, or 
a large database using up all your memory. 

Acknowledgments
------
Made by a human, for the same.
