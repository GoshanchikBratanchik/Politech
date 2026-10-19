// udpserver.cpp - UDP-сервер (ОС Linux, механизм select)
//
// Запуск:  udpserver FIRST_PORT LAST_PORT
//
// Сервер прослушивает диапазон UDP-портов FIRST_PORT..LAST_PORT (по одному
// сокету на порт). Все сокеты неблокирующие, готовность к чтению проверяется
// функцией select(), поэтому один поток обслуживает все порты и всех клиентов.
//
// Дейтаграмма от клиента содержит одно сообщение:
//   [4] номер, [4] дата, [4] время1, [4] время2, [4] длина N, [N] текст
// (числа 32-битные, сетевой порядок байт).
//
// Сообщение записывается в msg.txt в виде
//   IP:PORT dd.mm.yyyy hh:mm:ss hh:mm:ss Message
// (IP:PORT - адрес клиента). Повторы (сообщения клиента с уже принятым
// номером) в файл не записываются.
//
// В ответ клиенту отправляется дейтаграмма с номерами (4 байта, сетевой
// порядок) последних, не более 20, принятых от него сообщений. Ответ
// отправляется с того же порта, на который пришла дейтаграмма.
//
// Сервер хранит базу клиентов: ID клиента (IP + порт) -> номера принятых
// сообщений и время последней дейтаграммы. Клиенты, от которых не было
// дейтаграмм более 30 секунд, удаляются из базы.
//
// При получении сообщения с текстом "stop" сервер отправляет подтверждение
// и завершает работу.
//
// Компиляция: g++ -o udpserver udpserver.cpp

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <string>
#include <vector>
#include <deque>
#include <set>
#include <map>

#define MSG_FILE            "msg.txt"
#define HEADER_SIZE         20          // номер + дата + время1 + время2 + длина
#define MAX_DGRAM_SIZE      65536       // максимальный размер дейтаграммы
#define MAX_CONFIRMS        20          // сколько номеров отправляется в ответе
#define CLIENT_TIMEOUT_SEC  30          // время хранения неактивного клиента в базе
#define SELECT_TIMEOUT_SEC  1           // период проверки базы клиентов
#define MAX_RECV_PER_SOCKET 256         // дейтаграмм с одного сокета за один проход
#define SOCKET_RCVBUF       (1 << 20)   // размер буфера приема сокета

// Информация о клиенте
struct ClientInfo
{
    time_t                   last_time;  // время последней дейтаграммы
    std::set<unsigned int>   received;   // номера всех принятых сообщений
    std::deque<unsigned int> recent;     // последние принятые номера (новые - в конце)
};

// ID клиента: IP-адрес и порт (в сетевом порядке байт)
typedef std::pair<unsigned int, unsigned short> ClientId;
typedef std::map<ClientId, ClientInfo> ClientDb;

static ClientDb  g_db;                 // база клиентов
static FILE*     g_msg_file = 0;       // msg.txt
static bool      g_stop = false;       // получено сообщение "stop"
static char      g_buf[MAX_DGRAM_SIZE];

// ---------------------------------------------------------------------------

static int set_non_block_mode(int s)
{
    int fl = fcntl(s, F_GETFL, 0);
    return fcntl(s, F_SETFL, fl | O_NONBLOCK);
}

static unsigned int read_u32(const char* p)
{
    unsigned int n;
    memcpy(&n, p, 4);
    return ntohl(n);
}

static std::string client_name(const ClientId& id)
{
    struct in_addr a;
    char buf[64];

    a.s_addr = id.first;
    sprintf(buf, "%s:%u", inet_ntoa(a), (unsigned int)ntohs(id.second));
    return buf;
}

// Запись сообщения в msg.txt: "IP:PORT dd.mm.yyyy hh:mm:ss hh:mm:ss Message"
static void write_message(const std::string& peer, unsigned int date, unsigned int t1,
    unsigned int t2, const char* text, size_t len)
{
    fprintf(g_msg_file, "%s %02u.%02u.%04u %02u:%02u:%02u %02u:%02u:%02u ",
        peer.c_str(),
        date % 100, date / 100 % 100, date / 10000,
        t1 / 10000, t1 / 100 % 100, t1 % 100,
        t2 / 10000, t2 / 100 % 100, t2 % 100);
    fwrite(text, 1, len, g_msg_file);
    fputc('\n', g_msg_file);
    fflush(g_msg_file);
}

// Отметить номер idx как последний принятый (в списке последних номеров)
static void mark_recent(ClientInfo& info, unsigned int idx)
{
    std::deque<unsigned int>::iterator it;

    for (it = info.recent.begin(); it != info.recent.end(); ++it)
        if (*it == idx)
        {
            info.recent.erase(it);
            break;
        }

    info.recent.push_back(idx);
    while (info.recent.size() > MAX_CONFIRMS)
        info.recent.pop_front();
}

// Обработка одной дейтаграммы, принятой сокетом s
static void process_datagram(int s, const char* data, int size, const struct sockaddr_in* from)
{
    ClientId id(from->sin_addr.s_addr, from->sin_port);
    ClientDb::iterator it = g_db.find(id);
    std::string peer = client_name(id);
    bool is_stop = false;
    unsigned int idx;

    if (size < 4)
    {
        printf("Invalid datagram (%d bytes) from %s\n", size, peer.c_str());
        return;
    }

    if (it == g_db.end())
    {
        it = g_db.insert(std::make_pair(id, ClientInfo())).first;
        printf("New client: %s\n", peer.c_str());
    }

    ClientInfo& info = it->second;
    info.last_time = time(0);

    idx = read_u32(data);

    if (info.received.count(idx))
    {
        // Повтор: в файл не записывается, но номер будет в подтверждении
        mark_recent(info, idx);
    }
    else if (size >= HEADER_SIZE && read_u32(data + 16) <= (unsigned int)(size - HEADER_SIZE))
    {
        unsigned int len = read_u32(data + 16);
        const char* text = data + HEADER_SIZE;

        write_message(peer, read_u32(data + 4), read_u32(data + 8), read_u32(data + 12), text, len);

        info.received.insert(idx);
        mark_recent(info, idx);

        if (len == 4 && memcmp(text, "stop", 4) == 0)
            is_stop = true;
    }
    else
    {
        printf("Invalid datagram from %s\n", peer.c_str());
    }

    // Ответ: номера последних (не более 20) принятых сообщений, новые - первыми
    if (!info.recent.empty())
    {
        std::string reply;
        std::deque<unsigned int>::reverse_iterator r;

        for (r = info.recent.rbegin(); r != info.recent.rend(); ++r)
        {
            unsigned int n = htonl(*r);
            reply.append((const char*)&n, 4);
        }

        if (sendto(s, reply.data(), reply.size(), MSG_NOSIGNAL,
            (const struct sockaddr*)from, sizeof(*from)) < 0)
            printf("sendto() error to %s: %s\n", peer.c_str(), strerror(errno));
    }

    if (is_stop)
    {
        printf("'stop' message received from %s. Terminating...\n", peer.c_str());
        g_stop = true;
    }
}

// Прием всех дейтаграмм, находящихся во входном буфере сокета
static void process_socket(int s)
{
    int n;

    for (n = 0; n < MAX_RECV_PER_SOCKET && !g_stop; n++)
    {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        ssize_t r = recvfrom(s, g_buf, sizeof(g_buf), 0, (struct sockaddr*)&from, &fromlen);

        if (r < 0)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                printf("recvfrom() error: %s\n", strerror(errno));
            break; // дейтаграмм больше нет
        }

        process_datagram(s, g_buf, (int)r, &from);
    }
}

// Удаление из базы клиентов, от которых не было дейтаграмм более 30 секунд
static void cleanup_db()
{
    time_t now = time(0);
    ClientDb::iterator it = g_db.begin();

    while (it != g_db.end())
    {
        if (now - it->second.last_time > CLIENT_TIMEOUT_SEC)
        {
            printf("Client removed from db: %s\n", client_name(it->first).c_str());
            g_db.erase(it++);
        }
        else
        {
            ++it;
        }
    }
}

// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    std::vector<int> sockets;
    int first, last, port, maxfd = -1;
    size_t i;

    // stdout без буферизации: сообщения не теряются при аварийной остановке
    setvbuf(stdout, 0, _IONBF, 0);

    if (argc < 2)
    {
        printf("Usage: %s FIRST_PORT [LAST_PORT]\n", argv[0]);
        return 1;
    }

    first = atoi(argv[1]);
    last = (argc >= 3) ? atoi(argv[2]) : first;
    if (first <= 0 || first > 65535 || last < first || last > 65535)
    {
        printf("Error: invalid port range\n");
        return 1;
    }
    if (last - first + 1 > FD_SETSIZE - 16)
    {
        printf("Error: too many ports (max %d)\n", FD_SETSIZE - 16);
        return 1;
    }

    // Создание сокетов для всех портов диапазона
    for (port = first; port <= last; port++)
    {
        struct sockaddr_in addr;
        int bufsize = SOCKET_RCVBUF;
        int s = socket(AF_INET, SOCK_DGRAM, 0);

        if (s < 0 || s >= FD_SETSIZE)
        {
            printf("socket() error: %s\n", strerror(errno));
            return 1;
        }

        set_non_block_mode(s);
        // Увеличенный буфер приема: дейтаграммы от многих клиентов не теряются
        setsockopt(s, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((unsigned short)port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        {
            printf("Error: cannot bind UDP port %d: %s\n", port, strerror(errno));
            return 1;
        }

        sockets.push_back(s);
        if (s > maxfd)
            maxfd = s;
    }

    g_msg_file = fopen(MSG_FILE, "a");
    if (!g_msg_file)
    {
        printf("Error: cannot open %s\n", MSG_FILE);
        return 1;
    }

    printf("Listening UDP ports: %d-%d\n", first, last);

    while (!g_stop)
    {
        fd_set rfd;
        struct timeval tv;
        int r;

        // Набор fd_set формируется перед каждым вызовом select
        FD_ZERO(&rfd);
        for (i = 0; i < sockets.size(); i++)
            FD_SET(sockets[i], &rfd);

        tv.tv_sec = SELECT_TIMEOUT_SEC;
        tv.tv_usec = 0;

        r = select(maxfd + 1, &rfd, 0, 0, &tv);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            printf("select() error: %s\n", strerror(errno));
            break;
        }

        // Обработка сокетов, в которых есть дейтаграммы
        for (i = 0; r > 0 && i < sockets.size() && !g_stop; i++)
            if (FD_ISSET(sockets[i], &rfd))
                process_socket(sockets[i]);

        cleanup_db();
    }

    for (i = 0; i < sockets.size(); i++)
        close(sockets[i]);
    fclose(g_msg_file);

    return 0;
}
