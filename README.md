## DumDB

DumDB is a lightweight toy clone of MongoDB. It's not production quality and will probably never be.

There are loads of differences between DumDB and MongoDB.

DumDB

- Has only one collection
- Only supports a small number of operations
- Has no indexes (yet)
- Is just a http server

MongoDB

- Supports clusters
- Is thread safe
- Supports indexes
- Is performant
- Can be used in production

It's written in C and it uses:

- EmbeddableWebServer.h for the web server stuff
- jsmn.h for parsing JSON
- jsmn_stream for stream parsing JSON

Some system tests are written in plain javascript.

### Principles

The project is made to be run on multiple platforms (macos, linux and windows). I've tried to keep dependencies down to a minimum.

It's not there yet, but I want the server to use very little memory. In the future I might build memory caching, but then I still want the other parts of the server to be cheap memory wise.

### API

The server is exposing a restful JSON API.

The semi functional operations are:

- status
- /documents/insertOne
- /documents/findOne
- /documents/deleteOne

For testing the server also supports:

- reset (deletes the database, useful for testing)
- restart (rereads the database from file, also useful for testing)

### Algorithms

- Startup
  O(N) - Reads through the entire database file to find highest sequence id

- /documents/insertOne
  O(1) - It simply adds the document to the end of the file

- /documents/findOne
  O(N) - Reads through the entire database (or stops when it finds the document)

- /documents/deleteOne
  O(N) - Reads throught the entire database to find the document. Then tries to move the last document to the space left by the deleted document.

### To build and run (on macos):

clang -o build/dumdb_server src/main.c -Iinclude -lpthread && build/dumdb_server

curl -X POST http://localhost:8080/documents/insertOne \
 -H "Content-Type: application/json" \
 -d '{"name": "John Doe", "age": 30}'

curl -X POST http://localhost:8080/documents/find \
 -H "Content-Type: application/json" \
 -d '{"\_id": "000000000000000000000001"}'

You can also load the test/tests.html in a browser. Click the button to run the tests.

### Reason why it was built

I am fascinated by MongoDB and how it allows you to build something fast without planning too much. Then you can use indexes in many cases to still achieve the necessary performance. I wanted to have a sandbox to implement a simple version of MongoDB's indexes. I haven't actually gotten to that part in DumDB, and maybe I never will.

### Fun stuff to try

( ) Stop searching backwards to find where to truncate file. That should be done while parsing the file

( ) Create a reversed JSON parser, that reads the file from then end to find the last object

( ) Use off_t as type for positions in files

( ) documents/findOne - look at more than just \_id, but only equality at first

( ) documents/find - returning multiple documents. How to handle paging?

( ) documents/updateOne - something simple, just setting new fields or updating old?

( ) documents/count

( ) createIndex

( ) documents/aggegate - what's the smallest thing possible?

( ) make binary take flag --unsafe-testing, that enables reset/restart

( ) have a look at thread safety, how to not break things with multiple simultaneous write calls

( ) think about how it would work with multiple processes / a cluster of servers

( ) Modify jsmn_stream. Make it stop do any allocations or copying. It's enough to know the positions of the tags in the file.

( ) Create a reversed JSON parser, to find where the last document starts and ends

### Credits

- X for EmbeddableWebServer.h
- Y for jsmn.h
- Z for jsmn_stream
- Alex Chan for the base of the "unit test framework" in javascript (https://alexwlchan.net/2023/testing-javascript-without-a-framework/)

Example calls:

curl -X POST http://localhost:8080/test/reset \
-H "Content-Type: application/json" -d '{}'

curl -X POST http://localhost:8080/documents/insertOne \
 -H "Content-Type: application/json" \
 -d '{"name": "John Doe", "age": 30}'

{"\_id": "generatedDocumentId1"}

curl -X POST http://localhost:8080/documents/insertOne \
 -H "Content-Type: application/json" \
 -d '{"name": "Jane Doe", "age": 33}'

{"\_id": "generatedDocumentId2"}

curl -X POST http://localhost:8080/documents/find \
 -H "Content-Type: application/json" \
 -d '{"name": "John Doe"}'

[
{"_id": "generatedDocumentId1", "name": "John Doe", "age": 30}
]

curl -X POST http://localhost:8080/documents/find \
 -H "Content-Type: application/json" \
 -d '{}'

[
{"_id": "generatedDocumentId1", "name": "John Doe", "age": 30},
{"_id": "generatedDocumentId2", "name": "Jane Doe", "age": 33}
]

curl -X POST http://localhost:8080/documents/deleteOne \
 -H "Content-Type: application/json" \
 -d '{"\_id": "generatedDocumentId1"}'

{"deleteCount": 1}

curl -X POST http://localhost:8080/documents/updateOne \
 -H "Content-Type: application/json" \
 -d '{"filter": {"name": "Jane Doe"}, "update": {"$set": {"age": 34}}}'

{"updatedId": "generatedDocumentId2"}

curl -X POST http://localhost:8080/indexes/create \
 -H "Content-Type: application/json" \
 -d '{"fields": {"name": 1, "age": -1}, "name": "name*1_age*-1"}'

{"name": "name*1_age*-1"}

curl -X POST http://localhost:8080/documents/count \
 -H "Content-Type: application/json" \
 -d '{}'

{"count": 1}
