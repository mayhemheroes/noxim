FROM --platform=linux/amd64 ubuntu:22.04 as builder

RUN apt-get update
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential

COPY . /repo
WORKDIR /repo/other
RUN make -j8

FROM ubuntu:22.04 as package
COPY --from=builder /repo/other/ttable_from_hub /
