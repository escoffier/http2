FROM ubuntu:22.04

RUN apt update && apt -y install nghttp2-server

#CMD ["bash"]
ENTRYPOINT ["nghttpd","--no-tls","-v","8080"]
