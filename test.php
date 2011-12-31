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
			'sock' => $sock,
		);
	}

	public function __call($name, $args){
		assert(isset($this->ops[$name]));
		$op = $this->ops[$name];
		$data = implode('', $args);
		$sock = $op['sock'];
		$sock->send($data);

		switch($op['type']){
		case ZMQ::SOCKET_PAIR:
		case ZMQ::SOCKET_REQ:
		case ZMQ::SOCKET_XREQ:
			$x = $sock->recv();
			return $x;
		
		default:
			return NULL;
		}
	}
}


$dbz = new DBZMQ();
$dbz->bind("get",ZMQ::SOCKET_REQ,"tcp://127.0.0.1:17700");
$dbz->bind("put",ZMQ::SOCKET_PUSH,"tcp://127.0.0.1:17701");
$dbz->bind("del",ZMQ::SOCKET_PUSH,"tcp://127.0.0.1:17702");
$dbz->bind("walk",ZMQ::SOCKET_REQ,"tcp://127.0.0.1:17703");

// Contrived test sequence to validate the 'protocol'.
/*
Definition:

  get(k20) -> k ++ vN || k
  put(k20++vN) -> k ++ v || k
  del(k20)
  walk(k20) -> k ++ next(k) || k

With the key length being fixed at 20 bytes (160 bits) 
it allows for a protocol which can be easily expressed.

The get/put/del/walk primitives 
*/

$varA = str_pad("VARIABLE_A", 20);
$varB= str_pad("VARIABLE_B", 20);
$valA = str_pad("VALUE_A", 20);;
$valB = str_pad("VALUE_B", 20);;

assert(strlen($varA) == 20);               // sha1(a) = A
assert(strlen($varB) == 20);               // sha1(a) = B

// Verify Del() works
assert($dbz->del($varA) == NULL);          // del(A)
assert($dbz->del($varB) == NULL);          // del(B)
assert($dbz->get($varA) == $varA);         // get(A) = A
assert($dbz->get($varB) == $varB);         // get(B) = B

// Verify Put() and Get() work
assert($dbz->put($varA, $valA) == NULL);    // put(A ++ a)
assert($dbz->put($varB, $valB) == NULL);    // put(B ++ b)
assert($dbz->get($varA) == $varA . $valA);
assert($dbz->get($varB) == $varB . $valB);

// Verify Walk() works
assert($dbz->walk($varA) == $varA . $varB);
assert($dbz->walk($varB) == $varB);

// Clean up
assert($dbz->del($varA) == NULL);         
assert($dbz->del($varB) == NULL);         
