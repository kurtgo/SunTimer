#ifdef __ESP8266__
#include <SPIFFS.h>


#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <FS.h>
#include "filemgr.h"

#define DBG_OUTPUT_PORT Serial


//class FileBrowser {
	ESP8266_AT_WebServer *httpsvr = NULL;
	//holds the current upload
	File fsUploadFile;

	//format bytes
	String formatBytes(size_t bytes){
		if (bytes < 1024){
			return String(bytes)+"B";
		} else if(bytes < (1024 * 1024)){
			return String(bytes/1024.0)+"KB";
		} else if(bytes < (1024 * 1024 * 1024)){
			return String(bytes/1024.0/1024.0)+"MB";
		} else {
			return String(bytes/1024.0/1024.0/1024.0)+"GB";
		}
	}

	String getContentType(String filename){
		if(httpsvr->hasArg("download")) return "application/octet-stream";
		else if(filename.endsWith(".htm")) return "text/html";
		else if(filename.endsWith(".html")) return "text/html";
		else if(filename.endsWith(".css")) return "text/css";
		else if(filename.endsWith(".js")) return "application/javascript";
		else if(filename.endsWith(".png")) return "image/png";
		else if(filename.endsWith(".gif")) return "image/gif";
		else if(filename.endsWith(".jpg")) return "image/jpeg";
		else if(filename.endsWith(".ico")) return "image/x-icon";
		else if(filename.endsWith(".xml")) return "text/xml";
		else if(filename.endsWith(".pdf")) return "application/x-pdf";
		else if(filename.endsWith(".zip")) return "application/x-zip";
		else if(filename.endsWith(".gz")) return "application/x-gzip";
		return "text/plain";
	}

	bool handleFileRead(String path){
		DBG_OUTPUT_PORT.println("handleFileRead: " + path);
		if(path.endsWith("/")) path += "index.htm";
		String contentType = getContentType(path);
		String pathWithGz = path + ".gz";
		if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
			if(SPIFFS.exists(pathWithGz))
				path += ".gz";
			File file = SPIFFS.open(path, "r");
			size_t sent = httpsvr->streamFile(file, contentType);
			file.close();
			return true;
		}
		return false;
	}

	void handleFileUpload(){
		if(httpsvr->uri() != "/edit") return;
		HTTPUpload& upload = httpsvr->upload();
		if(upload.status == UPLOAD_FILE_START){
			String filename = upload.filename;
			if(!filename.startsWith("/")) filename = "/"+filename;
			DBG_OUTPUT_PORT.print("handleFileUpload Name: "); DBG_OUTPUT_PORT.println(filename);
			fsUploadFile = SPIFFS.open(filename, "w");
			filename = String();
		} else if(upload.status == UPLOAD_FILE_WRITE){
			//DBG_OUTPUT_PORT.print("handleFileUpload Data: "); DBG_OUTPUT_PORT.println(upload.currentSize);
			if(fsUploadFile)
				fsUploadFile.write(upload.buf, upload.currentSize);
		} else if(upload.status == UPLOAD_FILE_END){
			if(fsUploadFile)
				fsUploadFile.close();
			DBG_OUTPUT_PORT.print("handleFileUpload Size: "); DBG_OUTPUT_PORT.println(upload.totalSize);
		}
	}

	void handleFileDelete(){
		if(httpsvr->args() == 0) return httpsvr->send(500, "text/plain", "BAD ARGS");
		String path = httpsvr->arg(0);
		DBG_OUTPUT_PORT.println("handleFileDelete: " + path);
		if(path == "/")
			return httpsvr->send(500, "text/plain", "BAD PATH");
		if(!SPIFFS.exists(path))
			return httpsvr->send(404, "text/plain", "FileNotFound");
		SPIFFS.remove(path);
		httpsvr->send(200, "text/plain", "");
		path = String();
	}

	void handleFileCreate(){
		if(httpsvr->args() == 0)
			return httpsvr->send(500, "text/plain", "BAD ARGS");
		String path = httpsvr->arg(0);
		DBG_OUTPUT_PORT.println("handleFileCreate: " + path);
		if(path == "/")
			return httpsvr->send(500, "text/plain", "BAD PATH");
		if(SPIFFS.exists(path))
			return httpsvr->send(500, "text/plain", "FILE EXISTS");
		File file = SPIFFS.open(path, "w");
		if(file)
			file.close();
		else
			return httpsvr->send(500, "text/plain", "CREATE FAILED");
		httpsvr->send(200, "text/plain", "");
		path = String();
	}

	void handleFileList() {
		if(!httpsvr->hasArg("dir")) {httpsvr->send(500, "text/plain", "BAD ARGS"); return;}

		String path = httpsvr->arg("dir");
		DBG_OUTPUT_PORT.println("handleFileList: " + path);
		spiffs_DIR dir = SPIFFS.openDir(path);
		path = String();

		String output = "[";
		while(dir.next()){
			File entry = dir.openFile("r");
			if (output != "[") output += ',';
			bool isDir = false;
			output += "{\"type\":\"";
			output += (isDir)?"dir":"file";
			output += "\",\"name\":\"";
			output += String(entry.name()).substring(1);
			output += "\"}";
			entry.close();
		}

		output += "]";
		httpsvr->send(200, "text/json", output);
	}

	void filemgrSetup(ESP8266WebServer *s){
		httpsvr = s;
		DBG_OUTPUT_PORT.begin(115200);
		DBG_OUTPUT_PORT.print("\n");
		DBG_OUTPUT_PORT.setDebugOutput(true);
		SPIFFS.begin();
		{
			spiffs_DIR dir = SPIFFS.openDir("/");
			while (dir.next()) {
				String fileName = dir.fileName();
				size_t fileSize = dir.fileSize();
				DBG_OUTPUT_PORT.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
			}
			DBG_OUTPUT_PORT.printf("\n");
		}


		
		//SERVER INIT
		//list directory
		httpsvr->on("/list", HTTP_GET, handleFileList);
		//load editor
		httpsvr->on("/edit", HTTP_GET, [](){
			if(!handleFileRead("/edit.htm")) httpsvr->send(404, "text/plain", "FileNotFound");
		});
		//create file
		httpsvr->on("/edit", HTTP_PUT, handleFileCreate);
		//delete file
		httpsvr->on("/edit", HTTP_DELETE, handleFileDelete);
		//first callback is called after the request has ended with all parsed arguments
		//second callback handles file uploads at that location
		httpsvr->on("/edit", HTTP_POST, [](){ httpsvr->send(200, "text/plain", ""); }, handleFileUpload);

		//get heap status, analog input value and all GPIO statuses in one json call
		httpsvr->on("/info", HTTP_GET, [](){
			String json = "{";
			json += "\"heap\":"+String(ESP.getFreeHeap());
			json += ", \"analog\":"+String(analogRead(A0));
			json += ", \"gpio\":"+String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
			json += ", \"resetReason\":"+String(ESP.getResetReason());// returns String containing the last reset resaon in human readable format.
			json += ", \"resetReason\":"+String(ESP.getFreeHeap());// returns the free heap size.
			json += ", \"resetReason\":"+String(ESP.getChipId());// returns the ESP8266 chip ID as a 32-bit integer.
			json += ", \"resetReason\":"+String(ESP.getFlashChipId());// returns the flash chip ID as a 32-bit integer.
			json += ", \"resetReason\":"+String(ESP.getFlashChipSize());// returns the flash chip size, in bytes, as seen by the SDK (may be less than actual size).
			json += ", \"resetReason\":"+String(ESP.getFlashChipSpeed());// returns the flash chip frequency, in Hz.
			json += ", \"resetReason\":"+String(ESP.getCycleCount());// returns the cpu instruction cycle count since start as an unsigned 32-bit. This is useful for accurate timing of very short actions like bit banging.
			json += ", \"resetReason\":"+String(ESP.getVcc());// may be used to measure supply voltage. ESP needs to reconfigure the ADC at startup in order for this feature to be available. Add the following line to the top of your sketch to use getVcc:
			httpsvr->send(200, "text/json", json);
			json = String();
		});


	}
//};
#endif
