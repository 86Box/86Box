#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <86box/network/net_vserver_config.h>
#include <86box/network/net_vserver.h>

using bytes = std::vector<uint8_t>;

static constexpr uint32_t server_ip = 0x0A000204;
static constexpr uint32_t guest_ip = 0x0A00020F;

static void
capture(void *priv, const uint8_t *packet, int length)
{
    static_cast<std::vector<bytes> *>(priv)->emplace_back(packet, packet + length);
}

static void
put_be16(bytes &out, uint16_t value)
{
    out.push_back(uint8_t(value >> 8));
    out.push_back(uint8_t(value));
}

static void
put_be32(bytes &out, uint32_t value)
{
    put_be16(out, uint16_t(value >> 16));
    put_be16(out, uint16_t(value));
}

static uint16_t
get_be16(const uint8_t *data)
{
    return uint16_t((uint16_t(data[0]) << 8) | data[1]);
}

static uint32_t
get_be32(const uint8_t *data)
{
    return (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) | (uint32_t(data[2]) << 8) | data[3];
}

static bytes
ipv4(uint8_t protocol, uint32_t destination, const bytes &payload)
{
    bytes packet = { 0x45, 0 };
    put_be16(packet, uint16_t(20 + payload.size()));
    put_be16(packet, 0);
    put_be16(packet, 0);
    packet.push_back(128);
    packet.push_back(protocol);
    put_be16(packet, 0);
    put_be32(packet, guest_ip);
    put_be32(packet, destination);
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

static bytes
tcp_packet(uint16_t source_port, uint16_t destination_port, uint32_t seq, uint32_t ack, uint8_t flags,
           const bytes &payload = {})
{
    bytes tcp;
    put_be16(tcp, source_port);
    put_be16(tcp, destination_port);
    put_be32(tcp, seq);
    put_be32(tcp, ack);
    tcp.push_back(0x50);
    tcp.push_back(flags);
    put_be16(tcp, 8192);
    put_be16(tcp, 0);
    put_be16(tcp, 0);
    tcp.insert(tcp.end(), payload.begin(), payload.end());
    return ipv4(6, server_ip, tcp);
}

static bytes
udp_packet(uint16_t source_port, uint16_t destination_port, const bytes &payload)
{
    bytes udp;
    put_be16(udp, source_port);
    put_be16(udp, destination_port);
    put_be16(udp, uint16_t(8 + payload.size()));
    put_be16(udp, 0);
    udp.insert(udp.end(), payload.begin(), payload.end());
    return ipv4(17, server_ip, udp);
}

static std::string
tcp_payload(const bytes &packet)
{
    const int ip_header = int(packet[0] & 0x0F) * 4;
    const int tcp_header = int(packet[ip_header + 12] >> 4) * 4;
    const int total = get_be16(packet.data() + 2);

    return std::string(reinterpret_cast<const char *>(packet.data() + ip_header + tcp_header),
                       size_t(total - ip_header - tcp_header));
}

class FtpClient {
public:
    FtpClient(vserver_t *server, std::vector<bytes> *sent, uint16_t local_port)
        : local_port_(local_port), server_(server), sent_(sent)
    {}

    void connect(bool expect_greeting = true)
    {
        sent_->clear();
        send(0x02, {});
        ASSERT_FALSE(sent_->empty());
        const bytes syn_ack = sent_->back();
        ASSERT_EQ(get_be16(syn_ack.data() + 22), local_port_);
        server_next_ = get_be32(syn_ack.data() + 24) + 1;
        sent_->clear();
        send(0x10, {});
        if (expect_greeting) {
            ASSERT_FALSE(sent_->empty());
            EXPECT_NE(tcp_payload(sent_->back()).find("220 "), std::string::npos);
            server_next_ += uint32_t(tcp_payload(sent_->back()).size());
        } else {
            EXPECT_TRUE(sent_->empty());
        }
        sent_->clear();
    }

    std::string command(const std::string &text)
    {
        sent_->clear();
        bytes data(text.begin(), text.end());
        send(0x18, data);
        guest_next_ += uint32_t(data.size());
        const std::string reply = last_payload_to(local_port_);
        server_next_ += uint32_t(reply.size());
        return reply;
    }

    uint16_t passive_port()
    {
        const std::string reply = command("PASV\r\n");
        unsigned          a, b, c, d, high, low;

        EXPECT_EQ(std::sscanf(reply.c_str(), "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u)", &a, &b, &c, &d,
                              &high, &low),
                  6);
        return uint16_t((high << 8) | low);
    }

    void send(uint8_t flags, const bytes &payload)
    {
        const bytes packet = tcp_packet(local_port_, destination_port_, guest_next_, server_next_, flags, payload);
        vserver_process_ipv4(server_, packet.data(), int(packet.size()));
        if (flags & 0x02)
            guest_next_++;
    }

    uint16_t destination_port_ = 21;
    uint32_t guest_next_ = 100;
    uint32_t server_next_ = 0;
    uint16_t local_port_;

private:
    std::string last_payload_to(uint16_t destination)
    {
        for (auto iter = sent_->rbegin(); iter != sent_->rend(); ++iter) {
            if (get_be16(iter->data() + 22) == destination && (iter->at(33) & 0x08))
                return tcp_payload(*iter);
        }
        return {};
    }

    vserver_t *server_;
    std::vector<bytes> *sent_;
};

class FileServices : public ::testing::Test {
protected:
    void SetUp() override
    {
        server = vserver_init(nullptr, server_ip, false, false, true, true, capture, &sent);
        ASSERT_NE(server, nullptr);
    }

    void TearDown() override { vserver_close(server); }

    std::vector<bytes> sent;
    vserver_t *server = nullptr;
};

TEST_F(FileServices, FtpProvidesReadOnlyFileOverPassiveDataConnection)
{
    FtpClient control(server, &sent, 21000);
    control.connect();

    EXPECT_NE(control.command("USER anonymous\r\n").find("331 "), std::string::npos);
    EXPECT_NE(control.command("PASS guest@example.test\r\n").find("230 "), std::string::npos);
    EXPECT_NE(control.command("TYPE I\r\n").find("200 "), std::string::npos);
    uint16_t data_port = control.passive_port();

    FtpClient data(server, &sent, 21001);
    data.destination_port_ = data_port;
    data.connect(false);

    EXPECT_NE(control.command(std::string("RETR ") + VSERVER_FTP_TEST_FILE_NAME + "\r\n").find("150 "),
              std::string::npos);

    auto file_packet = std::find_if(sent.begin(), sent.end(), [data_port](const bytes &packet) {
        return get_be16(packet.data() + 20) == data_port && (packet[33] & 0x08);
    });
    ASSERT_NE(file_packet, sent.end());
    EXPECT_EQ(tcp_payload(*file_packet), VSERVER_FTP_TEST_FILE_CONTENT);
    const uint32_t data_server_next = get_be32(file_packet->data() + 24) + uint32_t(tcp_payload(*file_packet).size());
    sent.clear();

    data.server_next_ = data_server_next;
    data.send(0x10, {});
    ASSERT_FALSE(sent.empty());
    const bytes fin = sent.back();
    ASSERT_TRUE(fin[33] & 0x01);
    sent.clear();
    data.server_next_ = data_server_next + 1;
    data.send(0x10, {});

    auto completion = std::find_if(sent.begin(), sent.end(), [](const bytes &packet) {
        return get_be16(packet.data() + 22) == 21000 && (packet[33] & 0x08);
    });
    ASSERT_NE(completion, sent.end());
    EXPECT_NE(tcp_payload(*completion).find("226 "), std::string::npos);
}

TEST_F(FileServices, FtpSupportsActiveDataConnections)
{
    FtpClient control(server, &sent, 21004);
    control.connect();

    EXPECT_NE(control.command("USER anonymous\r\n").find("331 "), std::string::npos);
    EXPECT_NE(control.command("PASS any-password\r\n").find("230 "), std::string::npos);
    EXPECT_NE(control.command("PORT 10,0,2,15,195,80\r\n").find("200 "), std::string::npos);
    EXPECT_NE(control.command("EPRT |1|10.0.2.15|50000|\r\n").find("200 "), std::string::npos);
    EXPECT_NE(control.command(std::string("RETR ") + VSERVER_FTP_TEST_FILE_NAME + "\r\n").find("150 "),
              std::string::npos);

    auto active_syn = std::find_if(sent.begin(), sent.end(), [](const bytes &packet) {
        return get_be16(packet.data() + 20) == 20 && get_be16(packet.data() + 22) == 50000
            && (packet[33] & 0x02) && !(packet[33] & 0x10);
    });
    ASSERT_NE(active_syn, sent.end());
    const uint32_t server_syn_sequence = get_be32(active_syn->data() + 24);
    sent.clear();

    constexpr uint32_t client_sequence = 900;
    bytes active_syn_ack = tcp_packet(50000, 20, client_sequence, server_syn_sequence + 1, 0x12);
    EXPECT_TRUE(vserver_process_ipv4(server, active_syn_ack.data(), int(active_syn_ack.size())));
    auto file_packet = std::find_if(sent.begin(), sent.end(), [](const bytes &packet) {
        return get_be16(packet.data() + 20) == 20 && get_be16(packet.data() + 22) == 50000
            && (packet[33] & 0x08);
    });
    ASSERT_NE(file_packet, sent.end());
    EXPECT_EQ(tcp_payload(*file_packet), VSERVER_FTP_TEST_FILE_CONTENT);
    const uint32_t file_end_sequence = get_be32(file_packet->data() + 24)
                                     + uint32_t(tcp_payload(*file_packet).size());
    sent.clear();

    bytes file_ack = tcp_packet(50000, 20, client_sequence + 1, file_end_sequence, 0x10);
    EXPECT_TRUE(vserver_process_ipv4(server, file_ack.data(), int(file_ack.size())));
    auto fin = std::find_if(sent.begin(), sent.end(), [](const bytes &packet) {
        return get_be16(packet.data() + 20) == 20 && get_be16(packet.data() + 22) == 50000
            && (packet[33] & 0x01);
    });
    ASSERT_NE(fin, sent.end());
    const uint32_t fin_sequence = get_be32(fin->data() + 24);
    sent.clear();

    bytes fin_ack = tcp_packet(50000, 20, client_sequence + 1, fin_sequence + 1, 0x10);
    EXPECT_TRUE(vserver_process_ipv4(server, fin_ack.data(), int(fin_ack.size())));
    auto completion = std::find_if(sent.begin(), sent.end(), [](const bytes &packet) {
        return get_be16(packet.data() + 22) == 21004 && (packet[33] & 0x08);
    });
    ASSERT_NE(completion, sent.end());
    EXPECT_NE(tcp_payload(*completion).find("226 "), std::string::npos);
}

TEST_F(FileServices, FtpRejectsWritesAndRequiresAuthentication)
{
    FtpClient control(server, &sent, 21002);
    control.connect();

    EXPECT_NE(control.command("RETR ftp-test.txt\r\n").find("530 "), std::string::npos);
    EXPECT_NE(control.command("USER anonymous\r\n").find("331 "), std::string::npos);
    EXPECT_NE(control.command("PASS anything\r\n").find("230 "), std::string::npos);
    EXPECT_NE(control.command("STOR upload.txt\r\n").find("550 "), std::string::npos);
    EXPECT_NE(control.command(std::string("USER ") + VSERVER_LOGON_USERNAME + "\r\n").find("331 "),
              std::string::npos);
    EXPECT_NE(control.command("PASS wrong\r\n").find("530 "), std::string::npos);
    EXPECT_NE(control.command(std::string("PASS ") + VSERVER_LOGON_PASSWORD + "\r\n").find("230 "),
              std::string::npos);
    EXPECT_NE(control.command("PORT 10,0,2,16,195,80\r\n").find("501 "), std::string::npos);
    EXPECT_NE(control.command("EPRT |2|10.0.2.15|50000|\r\n").find("522 "), std::string::npos);
}

TEST_F(FileServices, TftpReadsTestFileAndReportsMissingFiles)
{
    bytes request = { 0, 1 };
    request.insert(request.end(), VSERVER_TFTP_TEST_FILE_NAME, VSERVER_TFTP_TEST_FILE_NAME
                                                               + strlen(VSERVER_TFTP_TEST_FILE_NAME) + 1);
    request.insert(request.end(), "octet", "octet" + sizeof("octet"));
    bytes rrq = udp_packet(20000, 69, request);

    EXPECT_TRUE(vserver_process_ipv4(server, rrq.data(), int(rrq.size())));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(get_be16(sent[0].data() + 20), 40000);
    EXPECT_EQ(get_be16(sent[0].data() + 22), 20000);
    EXPECT_EQ(get_be16(sent[0].data() + 28), 3);
    EXPECT_EQ(get_be16(sent[0].data() + 30), 1);
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(sent[0].data() + 32), sent[0].size() - 32),
              VSERVER_TFTP_TEST_FILE_CONTENT);

    sent.clear();
    bytes ack = udp_packet(20000, 40000, { 0, 4, 0, 1 });
    EXPECT_TRUE(vserver_process_ipv4(server, ack.data(), int(ack.size())));
    EXPECT_TRUE(sent.empty());

    sent.clear();
    bytes missing = { 0, 1 };
    missing.insert(missing.end(), "missing.txt", "missing.txt" + sizeof("missing.txt"));
    missing.insert(missing.end(), "octet", "octet" + sizeof("octet"));
    bytes missing_rrq = udp_packet(20000, 69, missing);
    EXPECT_TRUE(vserver_process_ipv4(server, missing_rrq.data(), int(missing_rrq.size())));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(get_be16(sent[0].data() + 28), 5);
    EXPECT_EQ(get_be16(sent[0].data() + 30), 1);

    sent.clear();
    bytes write_request = { 0, 2 };
    write_request.insert(write_request.end(), VSERVER_TFTP_TEST_FILE_NAME,
                         VSERVER_TFTP_TEST_FILE_NAME + strlen(VSERVER_TFTP_TEST_FILE_NAME) + 1);
    write_request.insert(write_request.end(), "octet", "octet" + sizeof("octet"));
    bytes wrq = udp_packet(20000, 69, write_request);
    EXPECT_TRUE(vserver_process_ipv4(server, wrq.data(), int(wrq.size())));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(get_be16(sent[0].data() + 28), 5);
    EXPECT_EQ(get_be16(sent[0].data() + 30), 2);
}
