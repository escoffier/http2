#include "event2/buffer.h"
#include "event2/bufferevent.h"
#include "event2/bufferevent_struct.h"
#include "event2/event_struct.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <event.h>
#include <event2/bufferevent_ssl.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <iostream>
#include <memory>
#include <nghttp2/nghttp2.h>
#include <ostream>
#include <string>
#include <string_view>

#define OUTPUT_WOULDBLOCK_THRESHOLD (1 << 16)

struct http2_stream_data {
  std::string request_path;
  int32_t stream_id;
};

struct http2_session_data {
  struct bufferevent *bev;
  nghttp2_session *session;
  std::string client_addr;
};

using SessionDeletor = void (*)(nghttp2_session *);
using CallbackDeletor = void (*)(nghttp2_session_callbacks *);

using session_unque_ptr = std::unique_ptr<nghttp2_session, SessionDeletor>;

void deleteSession(nghttp2_session *session) {
  if (session) {
    nghttp2_session_del(session);
  }
}

session_unque_ptr makeSessionUniquePtr(nghttp2_session_callbacks *callbacks,
                                       http2_session_data *session_data) {
  std::cout << "session callbacks: " << callbacks << std::endl;
  // nghttp2_session *session;
  nghttp2_session_server_new(&session_data->session, callbacks, session_data);
  std::cout << "make session: " << session_data->session << std::endl;
  nghttp2_session_callbacks_del(callbacks);
  return session_unque_ptr(session_data->session, deleteSession);
}

#define ARRLEN(x) (sizeof(x) / sizeof(x[0]))
class Connection {
public:
  Connection(http2_session_data *session_data);
  ~Connection(){};
  int64_t processData(std::string_view data);
  int64_t processData(const uint8_t *data, size_t len);

  int sendServerConnHeaer() {
    std::cout << "submits SETTINGS frame." << std::endl;
    nghttp2_settings_entry iv[1] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100}};
    auto rv = nghttp2_submit_settings(session_.get(), NGHTTP2_FLAG_NONE, iv,
                                      ARRLEN(iv));
    if (rv != 0) {
      std::cout << "Fatal error: " << nghttp2_strerror(rv) << std::endl;
      return -1;
    }
    return 0;
  }

private:
  session_unque_ptr session_;

public:
  static nghttp2_session_callbacks *callbacks_;
};

#define MAKE_NV(NAME, VALUE)                                                   \
  {                                                                            \
    (uint8_t *)NAME, (uint8_t *)VALUE, sizeof(NAME) - 1, sizeof(VALUE) - 1,    \
        NGHTTP2_NV_FLAG_NONE                                                   \
  }

int on_request_recv(nghttp2_session *session, uint32_t stream_id) {
  nghttp2_nv hdrs[] = {MAKE_NV(":status", "200")};
  std::cout << "send response , stream: " << stream_id << std::endl;
  auto rv =
      nghttp2_submit_response(session, stream_id, hdrs, ARRLEN(hdrs), nullptr);
  if (rv != 0) {
    std::cout << "Fatal error: " << nghttp2_strerror(rv) << std::endl;
    return -1;
  }
  return 0;
}

nghttp2_session_callbacks *callbacks() {
  nghttp2_session_callbacks *callbacks;
  nghttp2_session_callbacks_new(&callbacks);
  nghttp2_session_callbacks_set_on_header_callback(
      callbacks,
      [](nghttp2_session *, const nghttp2_frame *frame, const uint8_t *raw_name,
         size_t name_length, const uint8_t *raw_value, size_t value_length,
         uint8_t, void *user_data) -> int {
        std::cout << "on headers: (" << raw_name << " : " << raw_value << ")"
                  << std::endl;
        return 0;
      });
  nghttp2_session_callbacks_set_on_begin_headers_callback(
      callbacks,
      [](nghttp2_session *session, const nghttp2_frame *frame,
         void *user_data) -> int {
        std::cout << "stream id: " << frame->hd.stream_id << std::endl;
        auto stream_data = new http2_stream_data{"/", frame->hd.stream_id};
        nghttp2_session_set_stream_user_data(session, frame->hd.stream_id,
                                             stream_data);
        return 0;
      });

  nghttp2_session_callbacks_set_on_frame_recv_callback(
      callbacks,
      [](nghttp2_session *session, const nghttp2_frame *frame,
         void *user_data) -> int {
        std::cout << "receive " << int(frame->hd.type) << " frame "
                  << "<length=" << frame->hd.length
                  << ", flags=" << int(frame->hd.flags)
                  << ", stream_id=" << frame->hd.stream_id << ">" << std::endl;
        switch (frame->hd.type) {
        case NGHTTP2_DATA:
        case NGHTTP2_HEADERS:
          /* Check that the client request has finished */
          if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
            std::cout << "NGHTTP2_FLAG_END_STREAM" << std::endl;
            void *data = nghttp2_session_get_stream_user_data(
                session, frame->hd.stream_id);
            /* For DATA and HEADERS frame, this callback may be called
            after
               on_stream_close_callback. Check that stream still alive. */
            if (!data) {
              return 0;
            }
            auto stream_data = reinterpret_cast<http2_stream_data *>(data);
            //   return on_request_recv(session, session_data, stream_data);
            on_request_recv(session, stream_data->stream_id);
            return 0;
          }
          break;
        default:
          break;
        }
        return 0;
      });

  nghttp2_session_callbacks_set_send_callback(
      callbacks,
      [](nghttp2_session *session, const uint8_t *data, size_t length,
         int flags, void *user_data) -> ssize_t {
        std::cout << "send callback" << std::endl;
        auto session_data = reinterpret_cast<http2_session_data *>(user_data);
        auto bevt = session_data->bev;
        /* Avoid excessive buffering in server side. */
        if (evbuffer_get_length(bufferevent_get_output(session_data->bev)) >=
            OUTPUT_WOULDBLOCK_THRESHOLD) {
          return NGHTTP2_ERR_WOULDBLOCK;
        }
        bufferevent_write(session_data->bev, data, length);
        return (ssize_t)length;
      });

  std::cout << "create callbacks: " << callbacks << std::endl;
  return callbacks;
}

nghttp2_session_callbacks *Connection::callbacks_ = callbacks();

Connection::Connection(http2_session_data *session_data)
    : session_(nullptr, nullptr) {
  session_ = makeSessionUniquePtr(callbacks(), session_data);
}

int64_t Connection::processData(std::string_view data) {
  std::cout << "processData session: " << session_.get()
            << " data length: " << data.size() << std::endl;
  return nghttp2_session_mem_recv(
      session_.get(), reinterpret_cast<const uint8_t *>(data.data()),
      data.size());
}

int64_t Connection::processData(const uint8_t *data, size_t len) {
  return nghttp2_session_mem_recv(session_.get(), data, len);
}

int session_send(http2_session_data *session_data) {
  int rv;
  rv = nghttp2_session_send(session_data->session);
  if (rv != 0) {
    std::cout << "Fatal error: " << nghttp2_strerror(rv);
    return -1;
  }
  return 0;
}

void onConnection(struct evconnlistener *listener, evutil_socket_t fd,
                  struct sockaddr *addr, int socklen, void *userdata) {
  struct event_base *base = (struct event_base *)userdata;
  struct bufferevent *bevent;

  sockaddr_in *sin = (sockaddr_in *)addr;
  std::cout << "new connection from : " << sin->sin_port << std::endl;
  bevent = bufferevent_socket_new(
      base, fd, BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS);
  if (!bevent) {
    std::cout << "Error constructing bufferevent!";
    event_base_loopbreak(base);
    return;
  }
  bufferevent_enable(bevent, EV_READ | EV_WRITE);
  http2_session_data *session_data =
      new http2_session_data{bevent, nullptr, ""};
  auto connetion = new Connection(session_data);
  connetion->sendServerConnHeaer();
  bufferevent_setcb(
      bevent,
      [](struct bufferevent *bev, void *ctx) {
        http2_session_data *session_data = (http2_session_data *)ctx;

        evbuffer *input = bufferevent_get_input(bev);
        auto len = evbuffer_get_length(input);
        unsigned char *data = evbuffer_pullup(input, -1);
        auto connection = reinterpret_cast<Connection *>(ctx);
        std::cout << "received " << len << " bytes data: " << data << std::endl;
        // auto readlen = connection->processData(data, len);
        auto readlen = connection->processData(
            std::string_view(reinterpret_cast<char *>(data), len));
        if (readlen < 0) {
          std::cout << "Fatal err " << nghttp2_strerror(int(readlen));
          return;
        }
        std::cout << "processed " << readlen << " bytes data" << std::endl;

        if (evbuffer_drain(input, len) != 0) {
          std::cout << "Fatal error: evbuffer_drain failed" << std::endl;
          return;
        }
        if (session_send(session_data) != 0) {
          delete session_data;
          return;
        }
      },
      [](struct bufferevent *bev, void *ctx) {
        http2_session_data *session_data = (http2_session_data *)ctx;
        if (evbuffer_get_length(bufferevent_get_output(bev)) > 0) {
          return;
        }
        if (nghttp2_session_want_read(session_data->session) == 0 &&
            nghttp2_session_want_write(session_data->session) == 0) {
          delete session_data;
          return;
        }
        if (session_send(session_data) != 0) {
          delete session_data;
          return;
        }
      },
      [](struct bufferevent *bev, short events, void *ctx) {
        std::cout << "eventcb" << std::endl;
        auto connection = reinterpret_cast<Connection *>(ctx);
        if (events & BEV_EVENT_CONNECTED) {
          std::cout << "eventcb connected" << std::endl;
          connection->sendServerConnHeaer();
        }
        if (events & BEV_EVENT_EOF) {
          std::cout << "EOF" << std::endl;
          delete connection;
        } else if (events & BEV_EVENT_ERROR) {
          std::cout << "network error" << std::endl;
        } else if (events & BEV_EVENT_TIMEOUT) {
          std::cout << "timeout" << std::endl;
        }
      },
      (void *)connetion);
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
  addr.sin_port = htons(18080);
  addr.sin_family = AF_INET;
  listener = evconnlistener_new_bind(base, onConnection, base,
                                     LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE,
                                     -1, (sockaddr *)&addr, sizeof(addr));

  event_base_dispatch(base);

  evconnlistener_free(listener);
  event_base_free(base);

  // nghttp2_session_callbacks *callbacks;
  // nghttp2_session_callbacks_new(&callbacks);
  // nghttp2_session_callbacks_set_on_header_callback(
  //     callbacks,
  //     [](nghttp2_session *, const nghttp2_frame *frame, const uint8_t
  //     *raw_name,
  //        size_t name_length, const uint8_t *raw_value, size_t value_length,
  //        uint8_t, void *user_data) -> int {
  //       std::cout << "on headers" << std::endl;
  //       return 0;
  //     });

  // nghttp2_session *session;
  // nghttp2_session_server_new(&session, callbacks, nullptr);
  return 0;
};
