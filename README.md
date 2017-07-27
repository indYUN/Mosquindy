# Mosquindy
This is a MQTT client based on Mosquitto_sub + pub:
1.	Run as daemon.
2.	Combine sub and pub together.
3.	Sub to a topic:
  a)	uncompress payload which may be zipped.
  b)	Save payload to file.
  c)	Exec saved file if it’s BASH script.
4.	Pub agent for other program: 
  a)	UDP.
  b)	Protocol: qos=0;retain=0;topic=………;message=…………………….

