// tcpclient2.cpp - TCP-клиент для получения сообщений с сервера (ОС Linux)
//
// Запуск:  tcpclient2 IP:PORT get FILENAME
//
// Клиент подключается к серверу и отправляет команду "get" (3 байта).
// Сервер в ответ пересылает все сохраненные в msg.txt сообщения в формате
// протокола (номер, дата, время, время, длина, текст - все числа 32-битные в
// сетевом порядке байт) и после передачи последнего сообщения отключает клиента.
// Клиент сохраняет каждое принятое сообщение в файл FILENAME в виде:
//   IP:PORT dd.mm.yyyy hh:mm:ss hh:mm:ss Message
// где IP:PORT - адрес и порт сервера. Номер сообщения в файл не записывается.
//
// Компиляция: g++ -o tcpclient2 tcpclient2.cpp

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#define CONNECT_ATTEMPTS   10      // число попыток подключения
#define CONNECT_DELAY_MS   100     // пауза между попытками, мс
#define HEADER_SIZE        20      // номер + дата + время + время + длина

// ---------------------------------------------------------------------------
// Работа с сокетом
// ---------------------------------------------------------------------------

static void sleep_ms(int ms)
{
    usleep(ms * 1000);
}

// Разбор аргумента "IP:PORT" и заполнение адреса сервера
static bool parse_address(const char* arg, struct sockaddr_in* addr)
{
    std::string s(arg);
    size_t colon = s.rfind(':');
    std::string host, port;
    struct addrinfo hints, *res = 0;
    int port_num;

    if (colon == std::string::npos || colon == 0 || colon + 1 >= s.size())
        return false;

    host = s.substr(0, colon);
    port = s.substr(colon + 1);
    port_num = atoi(port.c_str());
    if (port_num <= 0 || port_num > 65535)
        return false;

    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons((unsigned short)port_num);

    if (inet_pton(AF_INET, host.c_str(), &addr->sin_addr) == 1)
        return true;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), 0, &hints, &res) != 0 || !res)
        return false;
    addr->sin_addr = ((struct sockaddr_in*)res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return true;
}

// Подключение к серверу: до CONNECT_ATTEMPTS попыток с паузой CONNECT_DELAY_MS
static int connect_to_server(const struct sockaddr_in* addr)
{
    int attempt;

    for (attempt = 1; attempt <= CONNECT_ATTEMPTS; attempt++)
    {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0)
        {
            printf("socket() error: %s\n", strerror(errno));
            return -1;
        }

        if (connect(s, (const struct sockaddr*)addr, sizeof(*addr)) == 0)
            return s;

        printf("Connect attempt %d failed: %s\n", attempt, strerror(errno));
        close(s);

        if (attempt < CONNECT_ATTEMPTS)
            sleep_ms(CONNECT_DELAY_MS);
    }

    return -1;
}

static bool send_all(int s, const char* data, size_t size)
{
    size_t sent = 0;

    while (sent < size)
    {
        ssize_t r = send(s, data + sent, size - sent, MSG_NOSIGNAL);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            printf("send() error: %s\n", strerror(errno));
            return false;
        }
        sent += (size_t)r;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Разбор принятых данных
// ---------------------------------------------------------------------------

// Чтение 32-битного числа в сетевом порядке байт
static unsigned int read_u32(const std::string& buf, size_t pos)
{
    unsigned int n;
    memcpy(&n, buf.data() + pos, 4);
    return ntohl(n);
}

// Запись сообщения в выходной файл:
// "IP:PORT dd.mm.yyyy hh:mm:ss hh:mm:ss Message"
static void write_message(FILE* f, const char* server, unsigned int date,
    unsigned int t1, unsigned int t2, const char* text, size_t len)
{
    fprintf(f, "%s %02u.%02u.%04u %02u:%02u:%02u %02u:%02u:%02u ",
        server,
        date % 100, date / 100 % 100, date / 10000,
        t1 / 10000, t1 / 100 % 100, t1 % 100,
        t2 / 10000, t2 / 100 % 100, t2 % 100);
    fwrite(text, 1, len, f);
    fputc('\n', f);
}

// Извлекает из буфера все полностью принятые сообщения и записывает их в файл.
// Обработанные данные удаляются из буфера. Возвращает число сообщений.
static unsigned int process_messages(std::string& buf, FILE* f, const char* server)
{
    size_t pos = 0;
    unsigned int cnt = 0;

    while (buf.size() - pos >= HEADER_SIZE)
    {
        // Поле номера сообщения (pos + 0) носит служебный характер и не сохраняется
        unsigned int date = read_u32(buf, pos + 4);
        unsigned int t1 = read_u32(buf, pos + 8);
        unsigned int t2 = read_u32(buf, pos + 12);
        unsigned int len = read_u32(buf, pos + 16);

        if (buf.size() - pos - HEADER_SIZE < len)
            break; // сообщение принято не полностью

        write_message(f, server, date, t1, t2, buf.data() + pos + HEADER_SIZE, len);
        pos += HEADER_SIZE + len;
        cnt++;
    }

    buf.erase(0, pos);
    return cnt;
}

// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    struct sockaddr_in addr;
    char server[64];
    char data[65536];
    std::string buf;
    unsigned int total = 0;
    bool ok = true;
    FILE* f;
    int s;

    // stdout без буферизации: сообщения не теряются при аварийной остановке
    setvbuf(stdout, 0, _IONBF, 0);

    if (argc < 4 || strcmp(argv[2], "get") != 0)
    {
        printf("Usage: %s IP:PORT get FILENAME\n", argv[0]);
        return 1;
    }

    if (!parse_address(argv[1], &addr))
    {
        printf("Invalid server address: %s\n", argv[1]);
        return 1;
    }

    // Строка "IP:PORT" сервера, которой предваряется каждое сообщение в файле
    sprintf(server, "%s:%u", inet_ntoa(addr.sin_addr), (unsigned int)ntohs(addr.sin_port));

    s = connect_to_server(&addr);
    if (s < 0)
    {
        printf("Error: failed to connect to %s after %d attempts\n", argv[1], CONNECT_ATTEMPTS);
        return 1;
    }
    printf("Connected to %s\n", server);

    f = fopen(argv[3], "w");
    if (!f)
    {
        printf("Cannot create file: %s\n", argv[3]);
        close(s);
        return 1;
    }

    // Команда серверу: передать все сохраненные сообщения
    if (!send_all(s, "get", 3))
        ok = false;

    // Прием данных до отключения сервером
    while (ok)
    {
        ssize_t r = recv(s, data, sizeof(data), 0);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            printf("recv() error: %s\n", strerror(errno));
            ok = false;
            break;
        }
        if (r == 0)
            break; // сервер передал все сообщения и закрыл соединение

        buf.append(data, (size_t)r);
        total += process_messages(buf, f, server);
    }

    if (ok && !buf.empty())
        printf("Warning: %u byte(s) of incomplete message discarded\n", (unsigned int)buf.size());

    fclose(f);
    close(s);

    printf("%u message(s) received and saved to %s\n", total, argv[3]);
    return ok ? 0 : 1;
}
