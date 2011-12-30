<?php

class DBZMQ{
	private $ops = array();
	/** @var ZMQContext */
	private $ctx;
	public function __construct(){
		$this->ctx = new ZMQContext();
	}
	public function bind($name, $type, $addr) {
		assert($name != 'bind');		
		$sock = $this->ctx->getSocket($type, __FILE__."-".$name);
		$sock->connect($addr);
		$this->ops[$name] = array(
			'type' => $type,
			'addr' => $addr,
//			'def' => $def,
			'sock' => $sock,
		);
	}

	public function __call($name, $args){
		assert(isset($this->ops[$name]));
		$op = $this->ops[$name];
		$data = implode('', $args);
		$sock = $op['sock'];
		$sock->send($data);
		if($op['type'] == ZMQ::SOCKET_REQ){
			return $sock->recv();
		}
		return NULL;
	}
}


$dbz = new DBZMQ();
$dbz->bind("put",ZMQ::SOCKET_PUSH,"tcp://127.0.0.1:17000", "put(a20,bN) = a,b");
$dbz->bind("get",ZMQ::SOCKET_REQ,"tcp://127.0.0.1:17001", "get(a20) = a,bN");
$dbz->bind("del",ZMQ::SOCKET_PUSH,"tcp://127.0.0.1:17002", "del(a20) = a");
$dbz->bind("walk",ZMQ::SOCKET_REQ,"tcp://127.0.0.1:17003", "walk(a20) = a,n20");

$k = sha1('test',TRUE);
$v = sha1('test2',TRUE);
var_dump($k);
var_dump($dbz->del($k));
var_dump($dbz->get($k) == $k);
var_dump($dbz->put($k,'Derp'));
var_dump($dbz->put($v,'Merp'));
var_dump($dbz->get($k) != $k);
var_dump($dbz->get($v) != $v);
var_dump($dbz->walk('a'));
var_dump($dbz->get($k));
var_dump($dbz->del($k));
