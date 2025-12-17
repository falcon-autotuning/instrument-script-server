#include <instrument-server/plugin/PluginInterface.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static HANDLE g_serial_handle = INVALID_HANDLE_VALUE;
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
static int g_serial_fd = -1;
#endif

static char g_device_path[256] = {0};

PluginMetadata plugin_get_metadata(void) {
  PluginMetadata meta = {0};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
  strncpy(meta.name, "Simple Serial", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.protocol_type, "SimpleSerial", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.description, "Example serial instrument plugin",
          PLUGIN_MAX_STRING_LEN - 1);
  return meta;
}

int32_t plugin_initialize(const PluginConfig *config) {
  // Parse connection JSON to get device path
  // Simplified parser (in production, use proper JSON library)
  const char *device_key = "\"device\": \"";
  const char *device_start = strstr(config->connection_json, device_key);
  if (!device_start) {
    return -1;
  }
  device_start += strlen(device_key);
  const char *device_end = strchr(device_start, '"');
  if (!device_end) {
    return -1;
  }

  size_t len = device_end - device_start;
  if (len >= sizeof(g_device_path)) {
    return -1;
  }

  strncpy(g_device_path, device_start, len);
  g_device_path[len] = '\0';

#ifdef _WIN32
  g_serial_handle =
      CreateFileA(g_device_path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

  if (g_serial_handle == INVALID_HANDLE_VALUE) {
    return -1;
  }

  DCB dcb = {0};
  dcb.DCBlength = sizeof(DCB);

  if (!GetCommState(g_serial_handle, &dcb)) {
    CloseHandle(g_serial_handle);
    return -1;
  }

  dcb.BaudRate = CBR_9600;
  dcb.ByteSize = 8;
  dcb.StopBits = ONESTOPBIT;
  dcb.Parity = NOPARITY;

  if (!SetCommState(g_serial_handle, &dcb)) {
    CloseHandle(g_serial_handle);
    return -1;
  }
#else
  g_serial_fd = open(g_device_path, O_RDWR | O_NOCTTY);
  if (g_serial_fd < 0) {
    return -1;
  }

  struct termios tty;
  if (tcgetattr(g_serial_fd, &tty) != 0) {
    close(g_serial_fd);
    return -1;
  }

  cfsetospeed(&tty, B9600);
  cfsetispeed(&tty, B9600);

  tty.c_cflag &= ~PARENB;
  tty.c_cflag &= ~CSTOPB;
  tty.c_cflag &= ~CSIZE;
  tty.c_cflag |= CS8;
  tty.c_cflag |= CREAD | CLOCAL;

  tty.c_lflag &= ~ICANON;
  tty.c_lflag &= ~ECHO;
  tty.c_lflag &= ~ISIG;

  tty.c_iflag &= ~(IXON | IXOFF | IXANY);
  tty.c_oflag &= ~OPOST;

  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 10;

  if (tcsetattr(g_serial_fd, TCSANOW, &tty) != 0) {
    close(g_serial_fd);
    return -1;
  }
#endif

  return 0;
}

int32_t plugin_execute_command(const PluginCommand *command,
                               PluginResponse *response) {
  strncpy(response->command_id, command->id, PLUGIN_MAX_STRING_LEN - 1);
  strncpy(response->instrument_name, command->instrument_name,
          PLUGIN_MAX_STRING_LEN - 1);

  // Build command string:  "VERB param1=val1 param2=val2\n"
  char cmd_buf[512];
  int pos = snprintf(cmd_buf, sizeof(cmd_buf), "%s", command->verb);

  for (uint32_t i = 0; i < command->param_count; i++) {
    const PluginParam *param = &command->params[i];

    switch (param->value.type) {
    case PARAM_TYPE_INT32:
      pos += snprintf(cmd_buf + pos, sizeof(cmd_buf) - pos, " %s=%d",
                      param->name, param->value.value.i32_val);
      break;
    case PARAM_TYPE_DOUBLE:
      pos += snprintf(cmd_buf + pos, sizeof(cmd_buf) - pos, " %s=%.6f",
                      param->name, param->value.value.d_val);
      break;
    case PARAM_TYPE_STRING:
      pos += snprintf(cmd_buf + pos, sizeof(cmd_buf) - pos, " %s=%s",
                      param->name, param->value.value.str_val);
      break;
    default:
      break;
    }
  }

  pos += snprintf(cmd_buf + pos, sizeof(cmd_buf) - pos, "\n");

  // Write command
#ifdef _WIN32
  DWORD written;
  if (!WriteFile(g_serial_handle, cmd_buf, pos, &written, NULL)) {
    response->success = false;
    response->error_code = GetLastError();
    strncpy(response->error_message, "Serial write failed",
            PLUGIN_MAX_STRING_LEN - 1);
    return -1;
  }
#else
  ssize_t written = write(g_serial_fd, cmd_buf, pos);
  if (written < 0) {
    response->success = false;
    response->error_code = -1;
    strncpy(response->error_message, "Serial write failed",
            PLUGIN_MAX_STRING_LEN - 1);
    return -1;
  }
#endif

  // Read response if expected
  if (command->expects_response) {
    char read_buf[512];

#ifdef _WIN32
    DWORD bytes_read;
    if (!ReadFile(g_serial_handle, read_buf, sizeof(read_buf) - 1, &bytes_read,
                  NULL)) {
      response->success = false;
      response->error_code = GetLastError();
      strncpy(response->error_message, "Serial read failed",
              PLUGIN_MAX_STRING_LEN - 1);
      return -1;
    }
#else
    ssize_t bytes_read = read(g_serial_fd, read_buf, sizeof(read_buf) - 1);
    if (bytes_read < 0) {
      response->success = false;
      response->error_code = -1;
      strncpy(response->error_message, "Serial read failed",
              PLUGIN_MAX_STRING_LEN - 1);
      return -1;
    }
#endif

    read_buf[bytes_read] = '\0';
    strncpy(response->text_response, read_buf, PLUGIN_MAX_PAYLOAD - 1);

    // Try to parse as number
    char *endptr;
    double val = strtod(read_buf, &endptr);
    if (endptr != read_buf) {
      response->return_value.type = PARAM_TYPE_DOUBLE;
      response->return_value.value.d_val = val;
    }
  }

  response->success = true;
  return 0;
}

void plugin_shutdown(void) {
#ifdef _WIN32
  if (g_serial_handle != INVALID_HANDLE_VALUE) {
    CloseHandle(g_serial_handle);
    g_serial_handle = INVALID_HANDLE_VALUE;
  }
#else
  if (g_serial_fd >= 0) {
    close(g_serial_fd);
    g_serial_fd = -1;
  }
#endif
}
