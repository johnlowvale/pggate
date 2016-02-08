/**
 * PGGate - P&G Gateway for vRoute
 * Copyright (c) Abivin JSC
 * @file    Main source file
 * @author  Dat Dinhquoc
 * @version 0.1
 *
 * Command-line commands:
 * To get help info: pggate -h
 * To create configuration file: pggate config -u USERNAME -p PASSWORD
 * To run PGGate server: pggate
 */
#include "stdafx.h"

//constants
#define CONFIG_FILE    "pggate.cfg"
#define DB_SERVER      "localhost"
#define DB_NAME        "pg"

#define HTTP_PORT      "8080"
#define STORE_LIST     "/store/list"
#define ORDER_LIST     "/order/list"
#define DRIVER_LIST    "/driver/list"
#define ROUTE_ENLIST   "/route/enlist"

#define ACCESS_DENIED  "ACCESS_DENIED"
#define ACCESS_ALLOWED "ACCESS_ALLOWED"
#define GET_ONLY       "GET_ONLY"
#define POST_ONLY      "POST_ONLY"

//clr namespaces
using namespace System;
using namespace System::Collections::Generic;
using namespace System::Data::SqlClient;
using namespace System::IO;
using namespace System::Net;
using namespace System::Runtime::Serialization::Json;
using namespace System::Security::Cryptography;
using namespace System::Text;

/**
 * Base request param class
 */
[Serializable]
ref class BaseParam {
public:
  String^ username;
  String^ password;
};

/**
 * Store list request param class
 */
[Serializable]
ref class StoreListParam:BaseParam {
public:
  String^ distributorId;
  String^ branchId;
};

/**
 * Order list request param class
 */
[Serializable]
ref class OrderListParam:BaseParam {
public:
  String^ distributorId;
  String^ branchId;
};

/**
 * Driver list request param class
 */
[Serializable]
ref class DriverListParam:BaseParam {
public:
  String^ distributorId;
  String^ branchId;
};

/**
 * Match driver and store class
 */
[Serializable]
ref class Match {
public:
  String^ driverId;
  String^ storeId;
};

/**
 * Route enlist request param class
 */
[Serializable]
ref class RouteEnlistParam:BaseParam {
public:
  String^ distributorId;
  String^ branchId;
  array<Match^>^ matches;
};

/**
 * Store class
 */
[Serializable]
ref class Store {
public:
  int     id;
  String^ name;
  String^ address;
};

/**
 * Store list result
 */
[Serializable]
ref class StoreList {
public:
  List<Store^>^ storeList;
};

/**
 * PGGate main class
 */
ref class PgGate {
public:
  SqlConnection^ connection;

  String^ username;
  String^ password;
  String^ passwordSha1;

  HttpListenerRequest^  request;
  HttpListenerResponse^ response;
  String^ requestBody;

  //miscs
  void writeline(String^ text);
  void exit();

  //http send
  void send(String^ text);
  void sendError(String^ message);

  //handlers
  void handleRoot();
  void handleStoreList();
  void handleOrderList();
  void handleDriverList();
  void handleRouteEnlist();
  void handleNotFound();

  //sha1
  String^ byteHex(unsigned char byte);
  String^ sha1(String^ str);

  //programme arguments
  void checkArguments(array<String^>^ args);

  //programme configurations & access
  Dictionary<String^,String^>^ readSimpleIni(String^ filePath);
  void checkConfigFile();
  String^ checkAuthorisation();
  String^ checkAuthedPost();

  //request & response utilities
  String^ getRequestBody();
  Stream^ stringToStream(String^ str);
  String^ streamToString(Stream^ stream);
  Object^ jsonToObject(Type^ type,String^ jsonStr);
  String^ objectToJson(Type^ type,Object^ object);

  //sql server
  void createSqlConnection();
  SqlDataReader^ sqlQuery(String^ commandStr);

  //main method
  int run(array<String^>^ args);
};

/**
 * Write a text to console
 */
void PgGate::writeline(String^ text) {
  Console::WriteLine(text);
}

/**
 * Exit programme (CLR console)
 */
void PgGate::exit() {
  Console::ReadLine();
  Environment::Exit(0);
}

/**
 * Send text to client and close response
 */
void PgGate::send(String^ text) {

  //get bytes
  UTF8Encoding^ encoding = gcnew UTF8Encoding();
  array<unsigned char>^ bytes = encoding->GetBytes(text);

  //write to response stream and close stream
  Stream^ output = response->OutputStream;
  output->Write(bytes,0,bytes->Length);
  output->Close();
}

/**
 * Send error in json format
 */
void PgGate::sendError(String^ message) {
  message = message->Replace("\"","'");
  send("{error:\""+message+"\"}");
}

/**
 * Handle root url request
 */
void PgGate::handleRoot() {
  if (request->HttpMethod!="GET") {
    sendError(GET_ONLY);
    return;
  }

  String^ text = "";
  text += "PGGate is online at port "+HTTP_PORT+"\n";
  text += "Please ask administrator for access details!";
  send(text);
}

/**
 * Handle store/list
 */
void PgGate::handleStoreList() {
  String^ authedPost = checkAuthedPost();
  if (authedPost->Length>0) {
    sendError(authedPost);
    return;
  }

  //get request params
  StoreListParam^ param = gcnew StoreListParam();
  try {
    param = (StoreListParam^)jsonToObject(param->GetType(),requestBody);
  }
  catch (Exception^ exception) {
    sendError(exception->Message);
  }

  //get all store rows from SQL server
  StoreList^ result = gcnew StoreList();
  result->storeList = gcnew List<Store^>();

  int count = 0;
  SqlDataReader^ reader = sqlQuery("select * from stores");
  while (reader->Read()) {
    Store^ store   = gcnew Store();
    store->id      = Int32::Parse(reader["id"]->ToString());
    store->name    = reader["name"]->ToString();
    store->address = reader["address"]->ToString();

    result->storeList->Add(store);
    count++;
  }
  reader->Close();

  //send result to requester
  writeline("Found "+count.ToString()+" store(s)");
  send(objectToJson(result->GetType(),result));
}

/**
 * Handle order/list
 */
void PgGate::handleOrderList() {
  String^ authedPost = checkAuthedPost();
  if (authedPost->Length>0) {
    sendError(authedPost);
    return;
  }

  //get request params
  OrderListParam^ param = gcnew OrderListParam();
  try {
    param = (OrderListParam^)jsonToObject(param->GetType(),requestBody);
  }
  catch (Exception^ exception) {
    sendError(exception->Message);
  }

  send(objectToJson(param->GetType(),param));
}

/**
 * Handle driver/list
 */
void PgGate::handleDriverList() {
  String^ authedPost = checkAuthedPost();
  if (authedPost->Length>0) {
    sendError(authedPost);
    return;
  }

  //get request params
  DriverListParam^ param = gcnew DriverListParam();
  try {
    param = (DriverListParam^)jsonToObject(param->GetType(),requestBody);
  }
  catch (Exception^ exception) {
    sendError(exception->Message);
  }

  send(objectToJson(param->GetType(),param));
}

/**
 * Handle route/enlist
 */
void PgGate::handleRouteEnlist() {
  String^ authedPost = checkAuthedPost();
  if (authedPost->Length>0) {
    sendError(authedPost);
    return;
  }

  //get request params
  RouteEnlistParam^ param = gcnew RouteEnlistParam();
  try {
    param = (RouteEnlistParam^)jsonToObject(param->GetType(),requestBody);
  }
  catch (Exception^ exception) {
    sendError(exception->Message);
  }

  send(objectToJson(param->GetType(),param));
}

/**
 * Handle not-found request (no such url)
 */
void PgGate::handleNotFound() {
  sendError(gcnew String("NO_SUCH_URL"));
}

/**
 * Get hexadecimal presentation of a byte
 */
String^ PgGate::byteHex(unsigned char byte) {
  array<String^>^ hex = gcnew array<String^>{
    "0","1","2","3","4","5","6","7","8","9","a","b","c","d","e","f"
  };

  //high and low nibbles
  unsigned char high = byte/16;
  unsigned char low  = byte%16;

  //hexadecimal of byte
  return hex[high]+hex[low];
}

/**
 * Get SHA1 hash of a string
 */
String^ PgGate::sha1(String^ str) {
  if (String::IsNullOrEmpty(str))
    return "";

  //get bytes
  UTF8Encoding^ encoding = gcnew UTF8Encoding();
  array<unsigned char>^ bytes = encoding->GetBytes(str);

  //sha1 hash
  array<unsigned char>^ hashBytes = SHA1::Create()->ComputeHash(bytes);
  String^ hash = "";
  for (int index=0; index<hashBytes->Length; index++)
    hash += byteHex(hashBytes[index]);

  return hash;
}

/**
 * Check commandline arguments
 */
void PgGate::checkArguments(array<String^>^ args) {
  
  //help info
  if (args->Length>0 && args[0]=="-h") {
    String^ info = gcnew String("");
    info += "PGGate Command List\n\n";
    info += "Get this help information:\n";
    info += "pggate -h\n\n";
    info += "Create configuration file:\n";
    info += "pggate config -u USERNAME -p PASSWORD\n\n";
    info += "Run PGGate server:\n";
    info += "pggate";

    writeline(info);
    exit();
  }
  else

  //create configuration file if stated in arguments
  if (args->Length==5 && args[0]=="config" && 
  args[1]=="-u" && args[3]=="-p") {
    String^ username = args[2];
    String^ password = args[4];

    //create file
    String^ text = "";
    text += "username="+username+"\r\n";
    text += "password="+sha1(password)+"\r\n";
    File::WriteAllText(CONFIG_FILE,text);

    //info
    writeline("Config file '"+CONFIG_FILE+"' created!");
    exit();
  }
  else

  //with unknown arguments
  if (args->Length>0) {
    writeline("Unknown command-line argument(s) specified!");
    writeline("For help info, please type: pggate -h");
    exit();
  }
}

/**
 * Read simple .ini file (no comment lines allowed)
 */
Dictionary<String^,String^>^ PgGate::readSimpleIni(String^ filePath) {
  array<String^>^ lines = File::ReadAllLines(filePath);

  Dictionary<String^,String^>^ ini = gcnew Dictionary<String^,String^>();
  for (int index=0; index<lines->Length; index++) {
    String^ line = lines[index];
    int     pos  = line->IndexOf("=");
    if (pos==-1)
      continue;

    String^ left  = line->Substring(0,pos)->Trim();
    String^ right = line->Substring(pos+1)->Trim();
    ini->Add(left,right);
  }

  return ini;
}

/**
 * Check configuration file
 */
void PgGate::checkConfigFile() {
  if (!File::Exists(CONFIG_FILE)) {
    writeline("Error: No configuration file found!");
    exit();
  }

  //read config file
  Dictionary<String^,String^>^ config = readSimpleIni(CONFIG_FILE);
  try {
    username = config["username"];
    password = "";
    passwordSha1 = config["password"];  
  }
  catch (Exception^ exception) {
    writeline("Error: "+exception->Message);
    exit();
  }
  
  //check config data
  if (username->Length==0) {
    writeline("Error: Username is blank in config file!");
    exit();
  }
  if (passwordSha1->Length==0) {
    writeline("Error: Password is blank in config file!");
    exit();
  }
}

/**
 * Check authorisation
 */
String^ PgGate::checkAuthorisation() {
  BaseParam^ param = gcnew BaseParam();
  try {
    param = (BaseParam^)jsonToObject(param->GetType(),requestBody);
  }
  catch (Exception^ exception) {
    sendError(exception->Message);
    return "";
  }
  
  //check input vs config
  String^ hash = sha1(param->password);
  if (!param->username->Equals(username) || !hash->Equals(passwordSha1)) 
    return ACCESS_DENIED;  
  else
    return ACCESS_ALLOWED;
}

/**
 * Check if authorised and method post
 */
String^ PgGate::checkAuthedPost() {
  if (request->HttpMethod!="POST") 
    return POST_ONLY;

  //check authorisation
  String^ authResult = checkAuthorisation();
  if (authResult==ACCESS_DENIED)
    return ACCESS_DENIED;

  return "";
}

/**
 * Get body of a request
 */
String^ PgGate::getRequestBody() {
  if (!request->HasEntityBody)
    return "";

  //reader
  Stream^       stream   = request->InputStream;
  Encoding^     encoding = request->ContentEncoding;
  StreamReader^ reader   = gcnew StreamReader(stream,encoding);
  
  //read data
  String^ text = reader->ReadToEnd();
  reader->Close();
  stream->Close();

  return text;
}

/**
 * Create a memory stream from string
 */
Stream^ PgGate::stringToStream(String^ str) {
  UTF8Encoding^ encoding = gcnew UTF8Encoding();
  return gcnew MemoryStream(encoding->GetBytes(str));
}

/**
 * Read a whole stream into string
 */
String^ PgGate::streamToString(Stream^ stream) {
  UTF8Encoding^ encoding = gcnew UTF8Encoding();  
  StreamReader^ reader   = gcnew StreamReader(stream,encoding);
  return reader->ReadToEnd();
}

/**
 * Convert json string to object 
 */
Object^ PgGate::jsonToObject(Type^ type,String^ jsonStr) {
  DataContractJsonSerializer^ tool = gcnew DataContractJsonSerializer(type);

  //to object
  Object^ object = tool->ReadObject(stringToStream(jsonStr));

  //object as result
  return object;
}

/**
 * Convert object to json string
 */
String^ PgGate::objectToJson(Type^ type,Object^ object) {
  DataContractJsonSerializer^ tool = gcnew DataContractJsonSerializer(type);
  
  //to string
  MemoryStream^ stream = gcnew MemoryStream();
  tool->WriteObject(stream,object);
  stream->Seek(0,SeekOrigin::Begin);
  String^ jsonStr = streamToString(stream);

  //string as result
  return jsonStr;
}

/**
 * Create connection to SQL server
 */
void PgGate::createSqlConnection() {
  connection = gcnew SqlConnection(
    "Data Source="+DB_SERVER+";" +
    "Initial Catalog="+DB_NAME+";" +
    "Integrated Security=SSPI;"
  );

  try {
    connection->Open();    
    writeline("Connection to SQL Server successfully created");
    writeline(connection->ConnectionString+"\n");
  }
  catch (Exception^ exception) {
    writeline("Failed to create connection to SQL Server");
    writeline(exception->Message);
    exit();
  }  
}

/**
 * Sql query
 */
SqlDataReader^ PgGate::sqlQuery(String^ commandStr) {
  SqlCommand^ command = gcnew SqlCommand(commandStr,connection);
  SqlDataReader^ reader = command->ExecuteReader();
  return reader;
}

/**
 * PGGate entry point
 */
int PgGate::run(array<String^>^ args) {
  checkArguments(args);
  checkConfigFile();
  createSqlConnection();

  //http server
  HttpListener^ listener = gcnew HttpListener();

  //url list
  listener->Prefixes->Add("http://*:"+HTTP_PORT+"/");
  listener->Prefixes->Add("http://*:"+HTTP_PORT+STORE_LIST+"/");
  listener->Prefixes->Add("http://*:"+HTTP_PORT+ORDER_LIST+"/");
  listener->Prefixes->Add("http://*:"+HTTP_PORT+DRIVER_LIST+"/");
  listener->Prefixes->Add("http://*:"+HTTP_PORT+ROUTE_ENLIST+"/");

  //start server
  try {
    listener->Start();
    writeline("PGGate started at port "+HTTP_PORT+"...");
  }
  catch (Exception^ exception) {

    //without root admin (user 'administrator' on windows)
    //a permission grant must be made on certain user:
    //netsh http add urlacl url=http://*:PORT/PATH user=DOMAIN\USER
    writeline("Please run PGGate as root administrator!");
    writeline("Error: "+exception->Message);
    exit();
  }

  //request loop
  while (1) {

    //waiting for request
    writeline("");
    writeline("Waiting for request...");
    HttpListenerContext^ context = listener->GetContext();

    //request received
    request  = context->Request;
    response = context->Response;
    
    //print request info
    String^ rawUrl = request->RawUrl;    
    String^ method = request->HttpMethod;
    writeline(method+" "+rawUrl);
    
    //get request data
    requestBody = getRequestBody();

    //match handler
    if (rawUrl->Equals("/")) 
      handleRoot();
    else
    if (rawUrl->Equals(STORE_LIST))
      handleStoreList();
    else
    if (rawUrl->Equals(ORDER_LIST))
      handleOrderList();
    else
    if (rawUrl->Equals(DRIVER_LIST))
      handleDriverList();
    else
    if (rawUrl->Equals(ROUTE_ENLIST))
      handleRouteEnlist();
    else
      handleNotFound();
    
    //request processed
    writeline("Response sent.");
  }//request loop
}//run

/**
 * Programme entry point
 */
int main(array<String^>^ args) {
  PgGate^ pgGate = gcnew PgGate();
  pgGate->run(args);
}

//end of file