<?php
header("Content-Type: text/plain");
echo "Hello from a PHP CGI!\n";
echo "REQUEST_METHOD = " . getenv("REQUEST_METHOD") . "\n";
echo "QUERY_STRING   = " . getenv("QUERY_STRING") . "\n";
if (getenv("REQUEST_METHOD") === "POST") {
	echo "BODY = " . file_get_contents("php://stdin") . "\n";
}
?>
