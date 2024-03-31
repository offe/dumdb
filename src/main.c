#include "EmbeddableWebServer.h"
#define JSMN_STRICT
#define JSMN_PARENT_LINKS
#include "jsmn.h"

#pragma comment(lib, "ws2_32")
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

#ifdef _WIN32
#include <windows.h>
#else // Linux, macOS, and other Unix-like systems
#include <sys/stat.h>
#endif

#include "jsmn_stream.c"

static uint64_t sequence_number = 1;

#define ID_LENGTH 24

// This does not need to be used for strings read by jsmn, they are already escaped
char *escape_json_string(const char *input)
{
  int length = 1; // For the null terminator
  for (const char *p = input; *p; p++)
  {
    length++;
    if (*p == '\"' || *p == '\\')
    {
      length++;
    }
  }

  char *output = malloc(length * sizeof(char));
  if (!output)
  {
    return NULL;
  }

  char *q = output;
  for (const char *p = input; *p; p++)
  {
    if (*p == '\"' || *p == '\\')
    {
      *q++ = '\\';
    }
    *q++ = *p;
  }
  *q = '\0'; // Null-terminate the output string

  return output;
}

// TODO? Use heapstring from EmbeddableWebServer here instead of fixed buffer?
int stringify_inner(const char *json, jsmntok_t *tokens, int num_tokens, int pos, char *dest, int *destpos, char *extra_key, char *extra_value);

void stringify_primitive(const char *json, jsmntok_t *t, char *dest, int *destpos)
{
  strncpy(dest + *destpos, json + t->start, t->end - t->start);
  *destpos += t->end - t->start;
}

void stringify_string(const char *json, jsmntok_t *t, char *dest, int *destpos)
{
  dest[(*destpos)++] = '"';
  strncpy(dest + *destpos, json + t->start, t->end - t->start);
  *destpos += t->end - t->start;
  dest[(*destpos)++] = '"';
}

int stringify_inner(const char *json, jsmntok_t *tokens, int num_tokens, int pos, char *dest, int *destpos, char *extra_key, char *extra_value)
{

  jsmntok_t *t = &tokens[pos];
  if (t->type == JSMN_STRING)
  {
    stringify_string(json, t, dest, destpos);
    return 1; // Tokens handled
  }
  else if (t->type == JSMN_PRIMITIVE)
  {
    stringify_primitive(json, t, dest, destpos);
    return 1; // Tokens handled
  }

  int i, j;
  int count = t->size;

  if (t->type == JSMN_OBJECT)
  {
    dest[(*destpos)++] = '{';
    if (extra_key != NULL)
    {
      dest[(*destpos)++] = '"';
      strncpy(dest + *destpos, extra_key, strlen(extra_key));
      *destpos += strlen(extra_key);
      dest[(*destpos)++] = '"';
      dest[(*destpos)++] = ':';
      dest[(*destpos)++] = '"';
      strncpy(dest + *destpos, extra_value, strlen(extra_value));
      *destpos += strlen(extra_value);
      dest[(*destpos)++] = '"';
      if (count > 0)
      {
        dest[(*destpos)++] = ',';
      }
    }
    for (i = 0, j = pos + 1; i < count; i++)
    {
      j += stringify_inner(json, tokens, num_tokens, j, dest, destpos, NULL, NULL); // Key
      dest[(*destpos)++] = ':';
      j += stringify_inner(json, tokens, num_tokens, j, dest, destpos, NULL, NULL); // Value
      if (i < count - 1)
      {
        dest[(*destpos)++] = ',';
      }
    }
    dest[(*destpos)++] = '}';
  }
  else if (t->type == JSMN_ARRAY)
  {
    dest[(*destpos)++] = '[';
    for (i = 0, j = pos + 1; i < count; i++)
    {
      j += stringify_inner(json, tokens, num_tokens, j, dest, destpos, NULL, NULL); // Element
      if (i < count - 1)
      {
        dest[*destpos] = ',';
        (*destpos)++;
      }
    }
    dest[(*destpos)++] = ']';
  }
  return j - pos; // Return number of tokens processed in this call
}

int stringify(const char *json, jsmntok_t *tokens, int num_tokens, int pos, char *dest, int *destpos, char *extra_key, char *extra_value)
{
  int n = stringify_inner(json, tokens, num_tokens, pos, dest, destpos, extra_key, extra_value);
  dest[*destpos] = '\0';
  return n;
}

void generateHexId(uint64_t seq, char *idBuffer)
{
  snprintf(idBuffer, 25, "%024" PRIx64, seq);
}

long get_process_memory_usage()
{
  char cmd[64];
  FILE *pipe;
  long rss = 0;

  sprintf(cmd, "ps -o rss= -p %d", getpid());

  pipe = popen(cmd, "r");
  if (pipe == NULL)
  {
    printf("Failed to run command\n");
    return 0;
  }
  fscanf(pipe, "%ld", &rss);
  pclose(pipe);

  return rss * 1024; // Convert kilobytes to bytes
}

long long get_file_size(const char *filename)
{
#ifdef _WIN32
  // Windows implementation
  LARGE_INTEGER filesize;
  filesize.QuadPart = -1; // Default to -1 in case of failure

  // Convert filename to wide characters for CreateFileW
  wchar_t wfilename[MAX_PATH];
  MultiByteToWideChar(CP_UTF8, 0, filename, -1, wfilename, MAX_PATH);

  HANDLE hFile = CreateFileW(wfilename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile != INVALID_HANDLE_VALUE)
  {
    if (!GetFileSizeEx(hFile, &filesize))
    {
      filesize.QuadPart = -1; // In case GetFileSizeEx fails
    }
    CloseHandle(hFile);
  }
  return (long long)filesize.QuadPart;
#else
  // Unix-like systems implementation
  struct stat statbuf;
  if (stat(filename, &statbuf) == 0)
  {
    return (long long)statbuf.st_size;
  }
  else
  {
    return -1; // Indicate failure
  }
#endif
}
/*
void test_stringify() {
  char* json_string = "{\"a\": \"12\", \"bananas\": [1, 2, true, \"wo\"]}";
  int num_tokens;
  jsmn_parser parser;
  jsmntok_t tokens[128];

  jsmn_init(&parser);
  num_tokens = jsmn_parse(&parser, json_string, strlen(json_string), tokens, sizeof(tokens) / sizeof(tokens[0]));

  printf("%d\n", num_tokens);
  printf("%d\n", tokens[0].type);
  char dest[10240]; // Large enough buffer for the output
  int destpos = 0;
  stringify(json_string, tokens, num_tokens, 0, dest, &destpos);
  dest[destpos] = '\0'; // Null-terminate the output string
  printf("%s\n", dest);
}
*/

static int get_token_index_by_key(char *key, int parent, const char *js_buffer, jsmntok_t *tokens, int number_of_tokens)
{
  if (number_of_tokens == 0)
  {
    return -1;
  }
  if (tokens[parent].type != JSMN_OBJECT)
  {
    // printf("Token at object index is not an object\n");
    return -1;
  }
  for (int i = parent + 1; i < number_of_tokens; ++i)
  {
    // printf("%d\n", i);
    jsmntok_t *t = &tokens[i];
    if (t->parent < parent)
    {
      // We have left the parent's subtree
      // printf("We left the objects subtree at %d (%d < %ld)\n", i, t->parent, parent);
      return -1;
    }
    if (t->type == JSMN_STRING &&
        t->size == 1 &&
        t->parent == parent &&
        (int)strlen(key) == t->end - t->start &&
        strncmp(key, &js_buffer[t->start], t->end - t->start) == 0)
    {
      return i + 1;
    }
  }
  // printf("Didn't find the key\n");
  return -1;
}

const char *db_file_name = "default.ddb.json";

void reset_file()
{
  FILE *file;
  file = fopen(db_file_name, "w");
  if (file)
  {
    fprintf(file, "[\n]");
    fclose(file);
    printf("Did reset!\n");
  }
  else
  {
    printf("Couldn't open file to do reset!\n");
  }
}

void add_document_to_file(const char *jsonString)
{
  FILE *file;

  // Attempt to open the file in read+update mode, this does not truncate the file
  file = fopen(db_file_name, "r+");
  if (!file)
  {
    // File does not exist, create it
    file = fopen(db_file_name, "w");
    if (file)
    {
      fclose(file);
    }
    else
    {
      perror("Failed to create file");
      exit(EXIT_FAILURE);
    }
    file = fopen(db_file_name, "r+");
  }
  if (file)
  {
    // File exists, find the position of the last character
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);

    if (fileSize <= 2)
    {
      // File is empty or wrongly formatted; start a new JSON array
      fseek(file, 0, SEEK_SET);
      // fprint(file, "[");
      fputs("[", file);
    }
    else
    {
      // Seek to the last two characters, which should be "\n]"
      fseek(file, -2, SEEK_END);
      // Only add the comma if there's at least one object in the file
      if (ftell(file) > 1)
      {
        fputs(",", file);
      }
    }
    fprintf(file, "\n{\"s\":1,\"d\":%s}\n]", jsonString);
    fclose(file);
  }
}

typedef struct
{
  jsmn_stream_parser *parser;
  // The stuff we want after documents are parsed
  long document_container_start;
  long document_container_end;
  long document_start;
  long document_end;
  long s_pos;
  int document_s;
  char document_id[ID_LENGTH + 1];
  // State machine for extracting the interesting stuff
  long pos;
  bool in_document_container;
  bool in_document;
  bool next_is_s;
  bool next_is_document;
  bool next_is_id;
  bool document_read;
} ddb_document_parse_state;

void print_document_parse_state(ddb_document_parse_state *state)
{
  // printf("In document container: %d\n", state->in_document_container);
  // printf("In document: %d\n", state->in_document);
  //  printf("Next is s: %d\n", state->next_is_s);
  //  printf("Next is document: %d\n", state->next_is_document);
  //  printf("Next is id: %d\n", state->next_is_id);
  printf("Document s: %d\n", state->document_s);
  printf("Document id: %s\n", state->document_id);
  printf("Document start pos: %ld\n", state->document_start);
  printf("Document end pos: %ld\n\n", state->document_end);
  printf("Document container start pos: %ld\n", state->document_container_start);
  printf("Document container end pos: %ld\n\n", state->document_container_end);
}

void start_arr(void *user_arg)
{
  // ddb_document_parse_state *document_parse_state = (ddb_document_parse_state *)user_arg;
  // printf("Array started.\n");
}
void end_arr(void *user_arg)
{
  // printf("Array ended\n");
}
void start_obj(void *user_arg)
{
  ddb_document_parse_state *document_parse_state = (ddb_document_parse_state *)user_arg;
  // print_document_parse_state(document_parse_state);
  // printf("= Object started\n");
  document_parse_state->in_document_container = (document_parse_state->parser->stack_height == 1);
  if (document_parse_state->in_document_container)
  {
    document_parse_state->document_container_start = document_parse_state->pos;
    document_parse_state->document_container_end = -1;
    document_parse_state->document_start = -1;
    document_parse_state->document_end = -1;
    document_parse_state->s_pos = -1;
    document_parse_state->document_s = 0;
    document_parse_state->document_id[0] = '\0';
  }
  else if (document_parse_state->next_is_document)
  {
    document_parse_state->in_document = true;
    document_parse_state->next_is_document = false;
    document_parse_state->document_start = document_parse_state->pos;
  }
}
void end_obj(void *user_arg)
{
  ddb_document_parse_state *document_parse_state = (ddb_document_parse_state *)user_arg;
  if (document_parse_state->in_document)
  {
    if (document_parse_state->parser->stack_height == 4)
    {
      document_parse_state->document_end = document_parse_state->pos + 1;
      document_parse_state->in_document = false;
    }
  }
  if (
      document_parse_state->parser->stack_height == 2)
  {
    document_parse_state->document_container_end = document_parse_state->pos + 1;
    document_parse_state->in_document_container = false;
    document_parse_state->document_read = true;
  }
}

void obj_key(const char *key, size_t key_len, void *user_arg)
{
  ddb_document_parse_state *document_parse_state = (ddb_document_parse_state *)user_arg;
  if (document_parse_state->in_document_container)
  {
    if (strcmp(key, "s") == 0)
    {
      document_parse_state->next_is_s = true;
    }
    if (strcmp(key, "d") == 0)
    {
      document_parse_state->next_is_document = true;
    }
  }
  if (document_parse_state->in_document)
  {
    if (strcmp(key, "_id") == 0)
    {
      document_parse_state->next_is_id = true;
    }
  }
}

void str(const char *value, size_t len, void *user_arg)
{
  ddb_document_parse_state *document_parse_state = (ddb_document_parse_state *)user_arg;
  // print_document_parse_state(document_parse_state);
  if (document_parse_state->next_is_id)
  {
    if (len == 24)
    {
      strcpy(document_parse_state->document_id, value);
    }
    document_parse_state->next_is_id = false;
  }
}

void primitive(const char *value, size_t len, void *user_arg)
{
  ddb_document_parse_state *document_parse_state = (ddb_document_parse_state *)user_arg;
  // print_document_parse_state(document_parse_state);
  if (document_parse_state->next_is_s)
  {
    document_parse_state->s_pos = document_parse_state->pos - len;
    if (sscanf(value, "%d", &document_parse_state->document_s) != 1)
    {
      document_parse_state->document_s = 1;
    }
  }
  document_parse_state->next_is_s = false;
}

jsmn_stream_callbacks_t cbs = {
    start_arr,
    end_arr,
    start_obj,
    end_obj,
    obj_key,
    str,
    primitive};

uint64_t read_sequence_number()
{
  jsmn_stream_parser parser;
  ddb_document_parse_state document_parse_state = {&parser};
  FILE *infile = fopen(db_file_name, "r");
  if (infile == NULL)
  {
    return 0;
  }
  jsmn_stream_init(&parser, &cbs, &document_parse_state);

  uint64_t highest_id = 0;
  uint64_t new_id = 0;
  int ch;
  while ((ch = fgetc(infile)) != EOF)
  {
    jsmn_stream_parse(&parser, (char)ch);
    if (document_parse_state.document_read)
    {
      if (sscanf(document_parse_state.document_id, "%" SCNx64, &new_id) == 1)
      {
        if (new_id > highest_id)
        {
          highest_id = new_id;
        }
      }
      document_parse_state.document_read = false;
    }
    (document_parse_state.pos)++;
  }
  fclose(infile);
  return highest_id;
}

int find_one_document(char *_id, char *buffer)
{
  jsmn_stream_parser parser;
  ddb_document_parse_state document_parse_state = {&parser};
  FILE *infile = fopen(db_file_name, "r");
  jsmn_stream_init(&parser, &cbs, &document_parse_state);

  int ch;
  while ((ch = fgetc(infile)) != EOF)
  {
    jsmn_stream_parse(&parser, (char)ch);
    if (document_parse_state.document_read)
    {
      if (document_parse_state.document_s == 1 && 0 == strcmp(document_parse_state.document_id, _id))
      {
        long length = document_parse_state.document_end - document_parse_state.document_start;
        fseek(infile, document_parse_state.document_start, SEEK_SET);
        size_t readBytes = fread(buffer, 1, length, infile);

        buffer[readBytes] = '\0';
        fclose(infile);
        return 0;
      }
      document_parse_state.document_read = false;
    }
    (document_parse_state.pos)++;
  }
  fclose(infile);
  // Not found
  return -1;
}

void print_contents_between_positions(FILE *file, long start_pos, long end_pos)
{
  long original_pos = ftell(file);
  fseek(file, start_pos, SEEK_SET);

  char c;
  while (ftell(file) < end_pos && (c = fgetc(file)) != EOF)
  {
    putchar(c);
  }
  putchar('\n');
  fseek(file, original_pos, SEEK_SET);
}

void move_contents(FILE *file, long dest_start, long dest_end, long src_start, long src_end)
{
  /*
  ...},   |
  {},
  */
  long move_size = src_end - src_start;
  long remaining_size = dest_end - dest_start - move_size;
  char *buffer = (char *)malloc(move_size * sizeof(char));
  if (buffer == NULL)
  {
    perror("Memory allocation failed");
    exit(EXIT_FAILURE);
  }
  fseek(file, src_start, SEEK_SET);
  fread(buffer, sizeof(char), move_size, file);
  fseek(file, dest_start, SEEK_SET);
  fwrite(buffer, sizeof(char), move_size - 1, file);
  if (remaining_size < 4)
  {
    for (long i = 0; i < remaining_size; ++i)
    {
      fputc(' ', file);
    }
    fputc('}', file);
  }
  else
  {
    fputs("},\n{", file);
    for (long i = 0; i < remaining_size - 4; ++i)
    {
      fputc(' ', file);
    }
    fputc('}', file);
  }
  free(buffer);
}

void truncate_array(FILE *file, long pos)
{
  // This should not be necessary, make sure to know where to truncate file when parsing
  fseek(file, pos - 1, SEEK_SET);
  // Search for the comma or the start of the array
  int c;
  long prev_pos = ftell(file);
  while (prev_pos >= 0 && (c = fgetc(file)) != ',' && c != '[')
  {
    // printf("%c %ld\n", c, prev_pos);
    fseek(file, -2, SEEK_CUR); // Move two positions backward
    prev_pos = ftell(file);
  }
  // printf("%c %ld\n", c, prev_pos);
  fseek(file, prev_pos + (c == '[' ? 1 : 0), SEEK_SET);
  fputs("\n]", file);
#ifdef _WIN32
  _chsize(_fileno(file), ftell(file));
#else
  ftruncate(fileno(file), ftell(file));
#endif
}

int delete_one_document(char *_id)
{
  jsmn_stream_parser parser;
  ddb_document_parse_state document_parse_state = {&parser};
  FILE *infile = fopen(db_file_name, "r+");
  jsmn_stream_init(&parser, &cbs, &document_parse_state);

  bool document_deleted = false;
  long erased_area_start = -1;
  long erased_area_end = -1;
  long last_container_start = -1;
  long last_container_end = -1;
  bool erased_area_has_ended = false;
  long documents = 0;
  int ch;
  while ((ch = fgetc(infile)) != EOF)
  {
    jsmn_stream_parse(&parser, (char)ch);
    if (document_parse_state.document_read)
    {
      printf("An object parsed\n");
      print_document_parse_state(&document_parse_state);
      // print_contents_between_positions(infile, document_parse_state.document_container_start, document_parse_state.document_container_end);
      // Keep track of the last not deleted document in the file

      if (document_parse_state.document_s == 1)
      {
        documents++;
      }
      if (!document_deleted)
      {
        if (0 != strcmp(document_parse_state.document_id, _id))
        {
          if (document_parse_state.document_s == 0)
          {
            if (erased_area_start == -1)
            {
              erased_area_start = document_parse_state.document_container_start;
            }
          }
          else
          {
            erased_area_start = -1;
          }
        }
        else // The matching document found
        {
          if (document_parse_state.document_s == 1)
          {
            long original_pos = ftell(infile);
            fseek(infile, document_parse_state.s_pos, SEEK_SET);
            fputc('0', infile);
            document_deleted = true;
            documents--;
            fseek(infile, original_pos, SEEK_SET);
            if (erased_area_start == -1)
            {
              erased_area_start = document_parse_state.document_container_start;
            }
            erased_area_end = document_parse_state.document_container_end;
          }
        }
      }
      else
      {
        if (!erased_area_has_ended)
        {
          if (document_parse_state.document_s == 0)
          {
            erased_area_end = document_parse_state.document_container_end;
          }
          else
          {
            erased_area_has_ended = true;
          }
        }
        if (document_parse_state.document_s == 1)
        {
          last_container_start = document_parse_state.document_container_start;
          last_container_end = document_parse_state.document_container_end;
        }
      }
      document_parse_state.document_read = false;
    }
    (document_parse_state.pos)++;
  }
  /*
  if (erased_area_end != -1)
  {
    printf("Area to move the last document to: \n");
    print_contents_between_positions(infile, erased_area_start, erased_area_end);
  }
  */

  // printf("%ld %ld\n", documents, last_container_start);
  if (last_container_start != -1)
  {
    /*
    printf("Last document: \n");
    print_contents_between_positions(infile, last_container_start, last_container_end);
    */
    if (last_container_end - last_container_start <= erased_area_end - erased_area_start)
    {
      move_contents(infile, erased_area_start, erased_area_end, last_container_start, last_container_end);
      truncate_array(infile, last_container_start);
    }
  }
  else if (documents == 0)
  {
    // We have no documents in the file
    truncate_array(infile, 3);
  }
  fclose(infile);
  return document_deleted ? 0 : -1;
}

int main(int argc, char *argv[])
{
  sequence_number = read_sequence_number() + 1;
  return acceptConnectionsUntilStoppedFromEverywhereIPv4(NULL, 8080);
}

const char *corsHeaders =
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
    "Content-Type: application/json\r\n";

struct Response *createResponseForRequest(const struct Request *request, struct Connection *connection)
{
  // To handle CORS
  if (0 == strcmp(request->method, "OPTIONS"))
  {
    struct Response *response = responseAllocWithFormat(200, "OK", "application/json", "{}");
    response->extraHeaders = strdup(corsHeaders);
    printf("Responding to OPTIONS request: %s\n", request->pathDecoded);
    return response;
  }
  // Only POST method is accepted, return 404 for everything else
  if (0 != strcmp(request->method, "POST"))
  {
    return responseAllocWithFormat(404, "Not found", "application/json", "{ \"status\" : 404 }");
  }

  // Only valid Content-Type is application/json, for all calls
  const struct Header *contentTypeHeader = headerInRequest("Content-Type", request);
  if (contentTypeHeader != NULL && 0 != strcmp(contentTypeHeader->value.contents, "application/json"))
  {
    return responseAllocWithFormat(415, "Unsupported Media Type", "application/json", "{ \"status\": 415, \"message\": \"Only accepts content type application/json, not %s\" }", contentTypeHeader->value.contents);
  }

  int num_tokens;
  jsmn_parser parser;
  jsmntok_t tokens[128]; /* We expect no more than 128 tokens in incoming calls so far*/

  jsmn_init(&parser);
  num_tokens = jsmn_parse(&parser, request->body.contents, request->body.length, tokens, sizeof(tokens) / sizeof(tokens[0]));

  if (num_tokens < 0)
  {
    return responseAllocWithFormat(400, "Bad Request", "application/json", "{ \"status\": 400, \"message\": \"Malformed JSON\" }");
  }

  /* Assume the top-level element is an object */
  if (num_tokens < 1 || tokens[0].type != JSMN_OBJECT)
  {
    return responseAllocWithFormat(400, "Bad Request", "application/json", "{ \"status\": 400, \"message\": \"Malformed JSON. Top level element must be object\" }");
  }

  printf("POST for %s\n", request->pathDecoded);

  /////////////////
  // /test/reset //
  /////////////////
  if (0 == strcmp(request->pathDecoded, "/test/reset"))
  {
    reset_file();
    sequence_number = 1;

    struct Response *response = responseAllocWithFormat(200, "OK", "application/json", "{ \"message\": \"Database reset\" }");
    response->extraHeaders = strdup(corsHeaders);
    return response;
  }

  ///////////////////
  // /test/restart //
  ///////////////////
  if (0 == strcmp(request->pathDecoded, "/test/restart"))
  {
    sequence_number = read_sequence_number() + 1;

    struct Response *response = responseAllocWithFormat(200, "OK", "application/json", "{ \"message\": \"Database restarted\" }");
    response->extraHeaders = strdup(corsHeaders);
    return response;
  }

  /////////////////
  // /test/reset //
  /////////////////
  if (0 == strcmp(request->pathDecoded, "/test/reset"))
  {
    reset_file();
    sequence_number = read_sequence_number() + 1;

    struct Response *response = responseAllocWithFormat(200, "OK", "application/json", "{ \"message\": \"Database reset\" }");
    response->extraHeaders = strdup(corsHeaders);
    return response;
  }
  /////////////
  // /status //
  /////////////
  if (0 == strcmp(request->pathDecoded, "/status"))
  {
    struct Response *response = responseAllocWithFormat(200, "OK", "application/json", "{ \"status\": \"OK\", \"buildTime\": \"%s\", \"memory\": %ld, \"databaseSize\": %lld }", __TIMESTAMP__, get_process_memory_usage(), get_file_size(db_file_name));
    response->extraHeaders = strdup(corsHeaders);
    return response;
  }

  //////////////////////////
  // /documents/insertOne //
  //////////////////////////
  if (0 == strcmp(request->pathDecoded, "/documents/insertOne"))
  {
    char _id[ID_LENGTH + 1];
    generateHexId(sequence_number++, _id);
    // TODO: Make a streaming version of stringify to avoid the static alloc
    char document_as_json[10240];
    int pos = 0;
    stringify(request->body.contents, tokens, num_tokens, 0, document_as_json, &pos, "_id", _id);
    printf("%s\n", document_as_json);
    add_document_to_file(document_as_json);

    struct Response *response = responseAllocWithFormat(200, "OK", "application/json", "{ \"_id\": \"%s\" }", _id);
    response->extraHeaders = strdup(corsHeaders);
    return response;
  }

  ////////////////////////
  // /documents/findOne //
  ////////////////////////
  // TODO: Make handle more than just find on _id
  if (0 == strcmp(request->pathDecoded, "/documents/findOne"))
  {
    int id_index = get_token_index_by_key("_id", 0, request->body.contents, tokens, num_tokens);
    char _id[ID_LENGTH + 1];
    snprintf(_id, sizeof(_id), "%.*s", tokens[id_index].end - tokens[id_index].start, request->body.contents + tokens[id_index].start);

    char buffer[1024];
    int found = find_one_document(_id, buffer);
    struct Response *response;
    if (found == 0)
    {
      response = responseAllocWithFormat(200, "OK", "application/json", "%s", buffer);
    }
    else
    {
      response = responseAllocWithFormat(404, "Not found", "application/json", "{ \"status\": 404, \"message\": \"No document found\"}");
    }
    response->extraHeaders = strdup(corsHeaders);
    return response;
  }

  //////////////////////////
  // /documents/deleteOne //
  //////////////////////////
  // TODO: Make handle more than just find on _id
  if (0 == strcmp(request->pathDecoded, "/documents/deleteOne"))
  {
    int id_index = get_token_index_by_key("_id", 0, request->body.contents, tokens, num_tokens);
    char _id[ID_LENGTH + 1];
    snprintf(_id, sizeof(_id), "%.*s", tokens[id_index].end - tokens[id_index].start, request->body.contents + tokens[id_index].start);

    char buffer[1024];
    int found = delete_one_document(_id);
    struct Response *response;
    if (found == 0)
    {
      response = responseAllocWithFormat(200, "OK", "application/json", "{ \"status\": 200, \"message\": \"Document deleted\"}");
    }
    else
    {
      response = responseAllocWithFormat(404, "Not found", "application/json", "{ \"status\": 404, \"message\": \"No document found\"}");
    }
    response->extraHeaders = strdup(corsHeaders);
    return response;
  }

  // Unknown path
  char *json_escaped_path = escape_json_string(request->pathDecoded);
  struct Response *response = responseAllocWithFormat(404, "Not found", "application/json", "{ \"status\": 404, \"message\": \"Non-existing path: %s\"}", json_escaped_path);
  free(json_escaped_path);
  response->extraHeaders = strdup(corsHeaders);
  return response;
}

/*

*/