#include "event2/bufferevent.h"
#include "event2/event_struct.h"
#include <_types/_uint8_t.h>
#include <event.h>
#include <event2/bufferevent_ssl.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <iostream>
#include <nghttp2/nghttp2.h>
#include <string_view>
#include <sys/_types/_int64_t.h>

struct http2_stream_data {
  struct http2_stream_data *prev, *next;
  char *request_path;
  int32_t stream_id;
  int fd;
};

struct http2_session_data {
  struct http2_stream_data root;
  struct bufferevent *bev;
  // app_context *app_ctx;
  nghttp2_session *session;
  char *client_addr;
};

class Connection {
public:
  Connection();
  ~Connection();
  int64_t processData(std::string_view data);

private:
  nghttp2_session *session_;
};

int64_t Connection::processData(std::string_view data) {
  return nghttp2_session_mem_recv(
      session_, reinterpret_cast<const uint8_t *>(data.data()), data.size());
}

void onConnection(struct evconnlistener *listener, evutil_socket_t fd,
                  struct sockaddr *addr, int socklen, void *userdata) {
  struct event_base *base = (struct event_base *)userdata;
  struct bufferevent *bevent;

  sockaddr_in *sin = (sockaddr_in *)addr;
  std::cout << "new connection from : " << sin->sin_port << std::endl;
  bevent = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
  if (!bevent) {
    std::cout << "Error constructing bufferevent!";
    event_base_loopbreak(base);
    return;
  }

  bufferevent_setcb(
      bevent,
      [](struct bufferevent *bev, void *ctx) {
        evbuffer *input = bufferevent_get_input(bev);
        auto len = evbuffer_get_length(input);
        unsigned char *data = evbuffer_pullup(input, -1);
        

      },
      [](struct bufferevent *bev, void *ctx) {

      },
      [](struct bufferevent *bev, short what, void *ctx) {

      },
      nullptr);
  // bufferevent_setcb(bevent, EchoServer::onReadMessage,
  // EchoServer::onWriteMessage, EchoServer::onClose, NULL );

  bufferevent_enable(bevent, EV_WRITE | EV_READ);
  // char MESSAGE[] = "Hello, World!\n";
  // bufferevent_write(bevent, MESSAGE, sizeof(MESSAGE));
}

int main() {
  event_base *base;
  struct evconnlistener *listener;
  struct sockaddr_in addr;

  base = event_base_new();
  memset(&addr, 0, sizeof(sockaddr_in));
  addr.sin_port = htons(8080);
  addr.sin_family = AF_INET;
  listener = evconnlistener_new_bind(base, onConnection, base,
                                     LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE,
                                     -1, (sockaddr *)&addr, sizeof(addr));

  event_base_dispatch(base);

  evconnlistener_free(listener);
  event_base_free(base);

  nghttp2_session_callbacks *callbacks;
  nghttp2_session_callbacks_new(&callbacks);
  nghttp2_session_callbacks_set_on_header_callback(
      callbacks,
      [](nghttp2_session *, const nghttp2_frame *frame, const uint8_t *raw_name,
         size_t name_length, const uint8_t *raw_value, size_t value_length,
         uint8_t, void *user_data) -> int {
        std::cout << "on headers" << std::endl;
        return 0;
      });

  nghttp2_session *session;
  nghttp2_session_server_new(&session, callbacks, nullptr);
  return 0;
};