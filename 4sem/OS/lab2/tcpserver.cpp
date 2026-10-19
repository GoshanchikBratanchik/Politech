// tcpserver.cpp - TCP-сервер (ОС Windows, механизм WSAEvents)
//
// Запуск:  tcpserver PORT
//
// Сервер прослушивает TCP-порт PORT и параллельно обслуживает несколько
// клиентов в одном потоке. Все сокеты работают в неблокирующем режиме,
// события сокетов отслеживаются с помощью WSAEventSelect /
// WSAWaitForMultipleEvents / WSAEnumNetworkEvents.
//
// Протокол:
//   Клиент после подключения передает 3 байта команды: "put" или "get".
//
//   "put" - далее следуют сообщения:
//     [4] номер, [4] дата, [4] время1, [4] время2, [4] длина N, [N] текст
//     (все числа - 32 бита, сетевой порядок байт).
//     Каждое сообщение записывается в msg.txt в виде
//       IP:PORT dd.mm.yyyy hh:mm:ss hh:mm:ss Message
//     где IP:PORT - адрес клиента; клиенту отправляется подтверждение "ok".
//     Если текст сообщения равен "stop", сервер отправляет "ok", отключает
//     всех клиентов и завершает работу.
//
//   "get" (доп. задание) - сервер передает клиенту все сообщения из msg.txt
//     в формате протокола (с номерами с 0, без IP:PORT) и отключает клиента.
//     Если сообщений нет - клиент отключается сразу.
//
// Схема обслуживания (как в методических указаниях):
//   - событие g_ev_listen связано с прослушивающим сокетом (FD_ACCEPT);
//   - событие g_ev_clients - общее для всех клиентских сокетов
//     (FD_READ | FD_WRITE | FD_CLOSE), поэтому число клиентов не ограничено
//     значением WSA_MAXIMUM_WAIT_EVENTS;
//   - цикл: WSAWaitForMultipleEvents -> WSAResetEvent обоих событий ->
//     опрос WSAEnumNetworkEvents всех сокетов -> обработка событий.
//     События, возникшие между сбросом и опросом, не теряются: они остаются
//     во внутренней записи сокета и снова переводят событие в сигнальное состояние.
//
// Компиляция (Visual Studio): cl /EHsc tcpserver.cpp

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#define MSG_FILE            "msg.txt"
#define HEADER_SIZE         20          // номер + дата + время1 + время2 + длина
#define IO_BUF_SIZE         65536       // буфер для recv
#define MAX_RECV_PER_EVENT  16          // recv за одно событие FD_READ (чтобы не "застревать" на одном клиенте)
#define WAIT_TIMEOUT_MS     1000        // таймаут ожидания событий
#define STOP_FLUSH_TIMEOUT  5000        // сколько ждать отправки "ok" на сообщение stop, мс

// Режим работы клиента (определяется первыми 3 байтами)
enum ClientMode
{
    MODE_NONE,  // команда еще не получена
    MODE_PUT,   // клиент передает сообщения
    MODE_GET    // клиент запросил сохраненные сообщения
};

// Состояние подключенного клиента
struct Client
{
    SOCKET       s;                 // сокет клиента
    std::string  peer;              // строка "IP:PORT" клиента
    std::string  in;                // принятые, но еще не разобранные данные
    std::string  out;               // данные для отправки клиенту
    size_t       out_pos;           // сколько байт из out уже отправлено
    ClientMode   mode;              // режим работы
    bool         close_after_send;  // отключить клиента после отправки всех данных (get)
    bool         need_close;        // клиент должен быть отключен
    unsigned int msg_count;         // число принятых/отправленных сообщений

    Client() : s(INVALID_SOCKET), out_pos(0), mode(MODE_NONE),
        close_after_send(false), need_close(false), msg_count(0) {}
};

static SOCKET               g_ls = INVALID_SOCKET;  // прослушивающий сокет
static WSAEVENT             g_ev_listen;            // событие прослушивающего сокета
static WSAEVENT             g_ev_clients;           // общее событие клиентских сокетов
static std::vector<Client*> g_clients;              // подключенные клиенты
static FILE*                g_msg_file = 0;         // файл msg.txt (дозапись)
static bool                 g_stop = false;         // получено сообщение "stop"
static Client*              g_stop_client = 0;      // клиент, приславший "stop"
static char                 g_io_buf[IO_BUF_SIZE];  // общий буфер приема

// ---------------------------------------------------------------------------
// Вспомогательные функции
// ---------------------------------------------------------------------------

static int init()
{
    WSADATA wsa_data;
    return (0 == WSAStartup(MAKEWORD(2, 2), &wsa_data));
}

static void deinit()
{
    WSACleanup();
}

static int set_non_block_mode(SOCKET s)
{
    unsigned long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode);
}

// Чтение 32-битного числа в сетевом порядке байт из буфера
static unsigned int read_u32(const char* p)
{
    u_long n;
    memcpy(&n, p, 4);
    return ntohl(n);
}

// Добавление 32-битного числа в сетевом порядке байт в буфер
static void append_u32(std::string& buf, unsigned int value)
{
    u_long n = htonl(value);
    buf.append((const char*)&n, 4);
}

// Создание прослушивающего сокета. Возвращает INVALID_SOCKET при ошибке.
static SOCKET create_listening_socket(unsigned short port)
{
    struct sockaddr_in addr;
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);

    if (s == INVALID_SOCKET)
    {
        printf("socket() error: %d\n", WSAGetLastError());
        return INVALID_SOCKET;
    }

    set_non_block_mode(s);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) != 0)
    {
        printf("Error: cannot bind TCP port %u (error %d)\n", (unsigned int)port, WSAGetLastError());
        closesocket(s);
        return INVALID_SOCKET;
    }

    if (listen(s, SOMAXCONN) != 0)
    {
        printf("Error: listen() failed on TCP port %u (error %d)\n", (unsigned int)port, WSAGetLastError());
        closesocket(s);
        return INVALID_SOCKET;
    }

    return s;
}

// ---------------------------------------------------------------------------
// Запись сообщений в msg.txt и чтение их оттуда
// ---------------------------------------------------------------------------

// Запись сообщения: "IP:PORT dd.mm.yyyy hh:mm:ss hh:mm:ss Message"
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
}

// Чтение n цифр с позиции pos строки s
static bool parse_digits(const std::string& s, size_t pos, size_t n, unsigned int* out)
{
    unsigned int v = 0;
    size_t i;

    if (pos + n > s.size())
        return false;
    for (i = 0; i < n; i++)
    {
        char c = s[pos + i];
        if (c < '0' || c > '9')
            return false;
        v = v * 10 + (unsigned int)(c - '0');
    }
    *out = v;
    return true;
}

// Разбор строки msg.txt "IP:PORT dd.mm.yyyy hh:mm:ss hh:mm:ss Message".
// Префикс IP:PORT отбрасывается.
static bool parse_stored_line(const std::string& line, unsigned int* date,
    unsigned int* t1, unsigned int* t2, std::string* text)
{
    size_t p = line.find(' ');
    unsigned int dd, mm, yy, h, m, s;

    if (p == std::string::npos)
        return false;
    p++; // начало даты

    if (line.size() < p + 29)
        return false;

    // дата
    if (!parse_digits(line, p, 2, &dd) || line[p + 2] != '.' ||
        !parse_digits(line, p + 3, 2, &mm) || line[p + 5] != '.' ||
        !parse_digits(line, p + 6, 4, &yy) || line[p + 10] != ' ')
        return false;
    *date = yy * 10000 + mm * 100 + dd;

    // время 1
    if (!parse_digits(line, p + 11, 2, &h) || line[p + 13] != ':' ||
        !parse_digits(line, p + 14, 2, &m) || line[p + 16] != ':' ||
        !parse_digits(line, p + 17, 2, &s) || line[p + 19] != ' ')
        return false;
    *t1 = h * 10000 + m * 100 + s;

    // время 2
    if (!parse_digits(line, p + 20, 2, &h) || line[p + 22] != ':' ||
        !parse_digits(line, p + 23, 2, &m) || line[p + 25] != ':' ||
        !parse_digits(line, p + 26, 2, &s) || line[p + 28] != ' ')
        return false;
    *t2 = h * 10000 + m * 100 + s;

    *text = line.substr(p + 29);
    return true;
}

// Формирует в c->out все сообщения из msg.txt в формате протокола.
// Возвращает число сообщений.
static unsigned int load_stored_messages(Client* c)
{
    std::string data, line, text;
    unsigned int idx = 0, date, t1, t2;
    size_t start = 0;
    FILE* f;

    // Все записанные ранее сообщения должны попасть в файл
    fflush(g_msg_file);

    f = fopen(MSG_FILE, "rb");
    if (!f)
        return 0;

    for (;;)
    {
        size_t r = fread(g_io_buf, 1, sizeof(g_io_buf), f);
        if (r == 0)
            break;
        data.append(g_io_buf, r);
    }
    fclose(f);

    // Разбор файла по строкам
    while (start < data.size())
    {
        size_t end = data.find('\n', start);
        if (end == std::string::npos)
            end = data.size();

        line = data.substr(start, end - start);
        start = end + 1;

        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);

        if (line.empty() || !parse_stored_line(line, &date, &t1, &t2, &text))
            continue;

        append_u32(c->out, idx);
        append_u32(c->out, date);
        append_u32(c->out, t1);
        append_u32(c->out, t2);
        append_u32(c->out, (unsigned int)text.size());
        c->out.append(text);
        idx++;
    }

    return idx;
}

// ---------------------------------------------------------------------------
// Обслуживание клиентов
// ---------------------------------------------------------------------------

// Отправка накопленных в c->out данных. Сокет неблокирующий: если буфер
// отправки заполнен (WSAEWOULDBLOCK), отправка продолжится по событию FD_WRITE.
static void client_send(Client* c)
{
    while (c->out_pos < c->out.size())
    {
        size_t rest = c->out.size() - c->out_pos;
        int chunk = (rest > IO_BUF_SIZE) ? IO_BUF_SIZE : (int)rest;
        int r = send(c->s, c->out.data() + c->out_pos, chunk, 0);

        if (r > 0)
        {
            c->out_pos += (size_t)r;
            continue;
        }

        if (r == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
            return; // ждем FD_WRITE

        printf("  send() error to %s: %d\n", c->peer.c_str(), WSAGetLastError());
        c->need_close = true;
        return;
    }

    // Все данные отправлены
    c->out.clear();
    c->out_pos = 0;

    if (c->close_after_send)
        c->need_close = true;
}

// Разбор сообщений клиента в режиме "put"
static void process_put(Client* c)
{
    size_t pos = 0;
    bool written = false;

    while (c->in.size() - pos >= HEADER_SIZE)
    {
        const char* p = c->in.data() + pos;
        // p[0..3] - номер сообщения, носит служебный характер
        unsigned int date = read_u32(p + 4);
        unsigned int t1 = read_u32(p + 8);
        unsigned int t2 = read_u32(p + 12);
        unsigned int len = read_u32(p + 16);
        const char* text = p + HEADER_SIZE;

        if (c->in.size() - pos - HEADER_SIZE < len)
            break; // сообщение пришло не полностью - ждем остальные данные

        write_message(c->peer, date, t1, t2, text, len);
        written = true;
        c->msg_count++;
        pos += HEADER_SIZE + len;

        // Подтверждение приема сообщения
        c->out.append("ok", 2);

        if (len == 4 && memcmp(text, "stop", 4) == 0)
        {
            printf("'stop' message received from %s. Terminating...\n", c->peer.c_str());
            g_stop = true;
            g_stop_client = c;
            break;
        }
    }

    c->in.erase(0, pos);

    if (written)
        fflush(g_msg_file);

    client_send(c);
}

// Обработка команды "get": отправка клиенту всех сохраненных сообщений
static void start_get(Client* c)
{
    c->msg_count = load_stored_messages(c);
    c->close_after_send = true;
    printf("  'get' from %s: %u message(s) to send\n", c->peer.c_str(), c->msg_count);

    // Если сообщений нет - клиент отключается сразу (client_send выставит need_close)
    client_send(c);
}

// Обработка принятых от клиента данных
static void process_input(Client* c)
{
    if (c->mode == MODE_NONE)
    {
        if (c->in.size() < 3)
            return;

        if (c->in.compare(0, 3, "put") == 0)
        {
            c->mode = MODE_PUT;
            c->in.erase(0, 3);
        }
        else if (c->in.compare(0, 3, "get") == 0)
        {
            c->mode = MODE_GET;
            c->in.clear();
            start_get(c);
            return;
        }
        else
        {
            printf("  Invalid command from %s, disconnecting\n", c->peer.c_str());
            c->in.clear();
            c->need_close = true;
            return;
        }
    }

    if (c->mode == MODE_PUT)
        process_put(c);
    else
        c->in.clear(); // в режиме get входящие данные не ожидаются
}

// Прием данных от клиента (событие FD_READ / FD_CLOSE)
static void client_read(Client* c)
{
    int i;

    for (i = 0; i < MAX_RECV_PER_EVENT && !c->need_close; i++)
    {
        int r = recv(c->s, g_io_buf, sizeof(g_io_buf), 0);

        if (r > 0)
        {
            c->in.append(g_io_buf, (size_t)r);
            continue;
        }

        if (r == 0)
        {
            // Клиент закрыл соединение
            c->need_close = true;
            break;
        }

        if (WSAGetLastError() == WSAEWOULDBLOCK)
            break; // данные закончились; если придут еще - будет новое FD_READ

        c->need_close = true; // ошибка (например, соединение сброшено)
        break;
    }

    // Разбор принятых данных (даже если клиент уже отключился)
    if (!g_stop)
        process_input(c);
}

// Закрытие соединения с клиентом
static void client_close(Client* c)
{
    if (c->s != INVALID_SOCKET)
    {
        // Корректное завершение: оставшиеся в буфере данные будут доставлены
        shutdown(c->s, SD_SEND);
        closesocket(c->s);
        c->s = INVALID_SOCKET;
        printf("Peer disconnected: %s (%u message(s))\n", c->peer.c_str(), c->msg_count);
    }
}

// Прием всех ожидающих подключений (событие FD_ACCEPT)
static void accept_clients()
{
    while (!g_stop)
    {
        struct sockaddr_in addr;
        int addrlen = sizeof(addr);
        char peer[64];
        SOCKET cs = accept(g_ls, (struct sockaddr*)&addr, &addrlen);
        Client* c;

        if (cs == INVALID_SOCKET)
        {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK)
                printf("accept() error: %d\n", err);
            return;
        }

        // Неблокирующий режим и привязка сокета к общему событию клиентов
        set_non_block_mode(cs);
        if (WSAEventSelect(cs, g_ev_clients, FD_READ | FD_WRITE | FD_CLOSE) != 0)
        {
            printf("WSAEventSelect() error: %d\n", WSAGetLastError());
            closesocket(cs);
            continue;
        }

        sprintf(peer, "%s:%u", inet_ntoa(addr.sin_addr), (unsigned int)ntohs(addr.sin_port));

        c = new Client();
        c->s = cs;
        c->peer = peer;
        g_clients.push_back(c);

        printf("Peer connected: %s\n", c->peer.c_str());

        // Данные могли прийти еще до вызова WSAEventSelect
        client_read(c);
    }
}

// Удаление отключенных клиентов из списка
static void remove_closed_clients()
{
    size_t i, j = 0;

    for (i = 0; i < g_clients.size(); i++)
    {
        Client* c = g_clients[i];
        if (c->need_close && c != g_stop_client)
        {
            client_close(c);
            delete c;
        }
        else
        {
            g_clients[j++] = c;
        }
    }
    g_clients.resize(j);
}

// Завершение работы по сообщению "stop": отправка "ok" клиенту, приславшему
// "stop", и отключение всех клиентов
static void stop_server()
{
    size_t i;
    DWORD start = GetTickCount();
    Client* c = g_stop_client;

    // Дождаться, пока подтверждение будет передано в сокет
    while (c && c->s != INVALID_SOCKET && c->out_pos < c->out.size() && !c->need_close &&
        GetTickCount() - start < STOP_FLUSH_TIMEOUT)
    {
        fd_set wfd;
        struct timeval tv = { 0, 100 * 1000 };

        FD_ZERO(&wfd);
        FD_SET(c->s, &wfd);
        select(0, 0, &wfd, 0, &tv);
        client_send(c);
    }

    // Вычитать непрочитанные данные клиента, приславшего "stop": при закрытии
    // сокета с непрочитанными данными соединение сбрасывается (RST), и клиент
    // мог бы не получить последнее подтверждение
    if (c && c->s != INVALID_SOCKET)
    {
        int n = 0;
        while (n++ < MAX_RECV_PER_EVENT && recv(c->s, g_io_buf, sizeof(g_io_buf), 0) > 0)
            ;
    }

    for (i = 0; i < g_clients.size(); i++)
    {
        client_close(g_clients[i]);
        delete g_clients[i];
    }
    g_clients.clear();
    g_stop_client = 0;
}

// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    int port;

    // stdout без буферизации: сообщения не теряются при аварийной остановке
    setvbuf(stdout, 0, _IONBF, 0);

    if (argc < 2)
    {
        printf("Usage: %s PORT\n", argv[0]);
        return 1;
    }

    port = atoi(argv[1]);
    if (port <= 0 || port > 65535)
    {
        printf("Error: invalid port: %s\n", argv[1]);
        return 1;
    }

    if (!init())
    {
        printf("Error: WSAStartup failed\n");
        return 1;
    }

    g_ls = create_listening_socket((unsigned short)port);
    if (g_ls == INVALID_SOCKET)
    {
        deinit();
        return 1;
    }

    g_msg_file = fopen(MSG_FILE, "a");
    if (!g_msg_file)
    {
        printf("Error: cannot open %s\n", MSG_FILE);
        closesocket(g_ls);
        deinit();
        return 1;
    }

    // Создание событий и привязка прослушивающего сокета
    g_ev_listen = WSACreateEvent();
    g_ev_clients = WSACreateEvent();
    if (g_ev_listen == WSA_INVALID_EVENT || g_ev_clients == WSA_INVALID_EVENT ||
        WSAEventSelect(g_ls, g_ev_listen, FD_ACCEPT) != 0)
    {
        printf("Error: cannot create events (%d)\n", WSAGetLastError());
        closesocket(g_ls);
        fclose(g_msg_file);
        deinit();
        return 1;
    }

    printf("Listening TCP port: %d\n", port);

    while (!g_stop)
    {
        WSAEVENT events[2];
        WSANETWORKEVENTS ne;
        DWORD dw;
        size_t i;

        events[0] = g_ev_listen;
        events[1] = g_ev_clients;

        // Ожидание событий на сокетах
        dw = WSAWaitForMultipleEvents(2, events, FALSE, WAIT_TIMEOUT_MS, FALSE);
        if (dw == WSA_WAIT_FAILED)
        {
            printf("WSAWaitForMultipleEvents() error: %d\n", WSAGetLastError());
            break;
        }

        // Сброс событий до опроса сокетов
        WSAResetEvent(g_ev_listen);
        WSAResetEvent(g_ev_clients);

        // Новые подключения
        if (WSAEnumNetworkEvents(g_ls, 0, &ne) == 0 && (ne.lNetworkEvents & FD_ACCEPT))
            accept_clients();

        // События клиентских сокетов
        for (i = 0; i < g_clients.size() && !g_stop; i++)
        {
            Client* c = g_clients[i];

            if (c->need_close)
                continue;

            if (WSAEnumNetworkEvents(c->s, 0, &ne) != 0)
            {
                c->need_close = true;
                continue;
            }

            if (ne.lNetworkEvents & FD_READ)
                client_read(c);

            if ((ne.lNetworkEvents & FD_WRITE) && !c->need_close)
                client_send(c);

            if (ne.lNetworkEvents & FD_CLOSE)
            {
                // Вычитать оставшиеся данные и отключить клиента
                client_read(c);
                c->need_close = true;
            }

            // Перед отключением - попытка отправить оставшиеся подтверждения
            if (c->need_close && c->out_pos < c->out.size())
                client_send(c);
        }

        if (g_stop)
            break;

        remove_closed_clients();
    }

    // Отключение всех клиентов
    stop_server();

    closesocket(g_ls);
    WSACloseEvent(g_ev_listen);
    WSACloseEvent(g_ev_clients);
    fclose(g_msg_file);
    deinit();

    return 0;
}
