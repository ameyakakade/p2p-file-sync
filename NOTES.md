# Add TODO's, ideas, and insights about the project in this file

## Todo
- [ ] Implement server-client using SSL. Do not worry about discovery
      right now.

## Notes
### Might be useful for documentation
Gemini said using a "serverless" method where two tcp ports bind and
connect without "listening" is not a viable option. SSL needs a
server-client relationship. Better option is making the device both a
server that listens for connections and connects to device when
necessary. Gemini talked about "UDP discovery", sounds like a way of
using UDP which does not require handshake to "broadcast" that we are
a file server with our IP. The other devices can then recognize it and
connect to it if needed. Thus, every device is a server for receiving
files, and connects to another receiver for sending files.

## Openssl
Need to build openssl in order to get the appropriate headers. Maybe
should build openssl along with our project or use system wide openssl.

"Using SSL BIO might work for this method." -> Talked with gemini, too
overpowered for our applicationm, sticking with normal sockets.

## Helpful links
Socket Programing: https://web.stanford.edu/class/archive/cs/cs107/cs107.1238/lectures/20/quickref.html
TLS Examples: https://stackoverflow.com/questions/7698488/turn-a-simple-socket-into-an-ssl-socket

https://blog.netherlabs.nl/articles/2009/01/18/the-ultimate-so_linger-page-or-why-is-my-tcp-not-reliable
