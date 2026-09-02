# Add TODO's, ideas, and insights about the project in this file

## Todo
- [ ] Implement server-client using SSL. Do not worry about discovery
      right now. Later
- [ ] Implement merkle trees for checking file changes.

## Notes
Use mDNS for discovery. Every node in the network must be a client and
a server.

### Merkle trees for tracking files
Merkle Trees: https://youtu.be/qHMLy5JjbjQ?si=a96aL6ZA4SCxAo6I
Use merkle trees along with timestamps to track changes. Calculate the
hashes and compare the merkle tree only when the timestamps have
changed.

Use sqlite to store timestamps.

## Openssl
Need to build openssl in order to get the appropriate headers. Maybe
should build openssl along with our project or use system wide openssl.

"Using SSL BIO might work for this method." -> Talked with gemini, too
overpowered for our applicationm, sticking with normal sockets.

## Helpful links
Socket Programing: https://web.stanford.edu/class/archive/cs/cs107/cs107.1238/lectures/20/quickref.html
TLS Examples: https://stackoverflow.com/questions/7698488/turn-a-simple-socket-into-an-ssl-socket

https://blog.netherlabs.nl/articles/2009/01/18/the-ultimate-so_linger-page-or-why-is-my-tcp-not-reliable

## How do nodes decide server and client
### This is how it will work assuming they figure out who is server and client.
#### node 1
lets sync
i am now client
requesting tree
comparing both trees - calculating updates
client will perform updates.
bye bye


#### node 2
lets sync
i am now server
sending tree
ending

## How will nodes decide who is server and client?
No idea. Will figure it out.

Maybe this helps:
node 1
sent lets sync
waiting for response
node responded and wants me to be client.

node 2
got sync req
i am server
asking other node to be client.

## Will having many nodes trying to sync cause issues?