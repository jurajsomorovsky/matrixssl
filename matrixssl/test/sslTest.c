/*
 *	sslTest.c
 *	Release $Name: MATRIXSSL-3-4-0-OPEN $
 *
 *	Self-test of the MatrixSSL handshake protocol and encrypted data exchange.
 *	Each enabled cipher suite is run through all configured SSL handshake paths
 */
/*
 *	Copyright (c) 2013 INSIDE Secure Corporation
 *	Copyright (c) PeerSec Networks, 2002-2011
 *	All Rights Reserved
 *
 *	The latest version of this code is available at http://www.matrixssl.org
 *
 *	This software is open source; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This General Public License does NOT permit incorporating this software 
 *	into proprietary programs.  If you are unable to comply with the GPL, a 
 *	commercial license for this software may be purchased from INSIDE at
 *	http://www.insidesecure.com/eng/Company/Locations
 *	
 *	This program is distributed in WITHOUT ANY WARRANTY; without even the 
 *	implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 *	See the GNU General Public License for more details.
 *	
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *	http://www.gnu.org/copyleft/gpl.html
 */
/******************************************************************************/

#include "matrixssl/matrixsslApi.h"



/*
	This test application can also run in a mode that measures the time of
	SSL connections.  If USE_HIGHRES time is disabled the granularity is
	milliseconds so most non-embedded platforms will report 0 msecs/conn for
	most stats. 

	Standard handshakes and client-auth handshakes (commercial only) are timed
	for each enabled cipher suite. The other handshake types will still run
	but will not be timed
*/
//#define ENABLE_PERF_TIMING
#define CONN_ITER			500 /* number of connections per type of hs */
#define APP_DATA_LEN		1024

#ifdef ENABLE_PERF_TIMING
#define testTrace(x)
#define TIME_UNITS "msecs/connection\n"
#else /* !ENABLE_PERF_TIMING */
#define testTrace(x) _psTrace(x)
#endif /* ENABLE_PERF_TIMING */
 
/******************************************************************************/
/*
	Must define in matrixConfig.h:
		USE_SERVER_SIDE_SSL
		USE_CLIENT_SIDE_SSL
		USE_CLIENT_AUTH (commercial only) 
*/
#if !defined(USE_SERVER_SIDE_SSL) || !defined(USE_CLIENT_SIDE_SSL)
#error "Must enable both USE_SERVER_SIDE_SSL and USE_CLIENT_SIDE_SSL to test"
#endif
	
typedef struct {
    ssl_t               *ssl;
    sslKeys_t           *keys;
#ifdef ENABLE_PERF_TIMING
	uint32			runningTime;
#endif	
} sslConn_t;

typedef struct {
    char    name[52];
    int32   cipherId;
    int32   rsa;
    int32   dh;
    int32   psk;
    int32   ecdh;
} testCipherSpec_t;

static	sslSessionId_t	*clientSessionId;

static int32 newSessionFlag = 0;
/******************************************************************************/
/*
	Key loading.  The header files are a bit easier to work with because
	it is better to get a compile error that a header isn't found rather
	than a run-time error that a .pem file isn't found
*/
#define USE_HEADER_KEYS /* comment out this line to test with .pem files */

#if defined(MATRIX_USE_FILE_SYSTEM) && !defined(USE_HEADER_KEYS)
static char svrKeyFile[] = "../crypto/sampleCerts/RSA/1024_RSA_KEY.pem";
static char svrCertFile[] = "../crypto/sampleCerts/RSA/1024_RSA.pem";
static char svrCAfile[] = "../crypto/sampleCerts/RSA/1024_RSA_CA.pem";
static char clnCAfile[] = "../crypto/sampleCerts/RSA/2048_RSA_CA.pem";
static char clnKeyFile[] = "../crypto/sampleCerts/RSA/2048_RSA_KEY.pem";
static char clnCertFile[] = "../crypto/sampleCerts/RSA/2048_RSA.pem";
#endif  /* MATRIX_USE_FILE_SYSTEM && !USE_HEADER_KEYS */

					
#ifdef USE_HEADER_KEYS
#include "sampleCerts/RSA/1024_RSA_KEY.h"
#include "sampleCerts/RSA/1024_RSA.h"
#include "sampleCerts/RSA/1024_RSA_CA.h"
#include "sampleCerts/RSA/2048_RSA_CA.h"
#include "sampleCerts/RSA/2048_RSA_KEY.h"
#include "sampleCerts/RSA/2048_RSA.h"
#endif /* USE_HEADER_KEYS */

static void freeSessionAndConnection(sslConn_t *cpp);

static int32 initializeServer(sslConn_t *svrConn, testCipherSpec_t cipher);
static int32 initializeClient(sslConn_t *clnConn, testCipherSpec_t cipher,
				 sslSessionId_t *sid);

static int32 initializeHandshake(sslConn_t *clnConn, sslConn_t *svrConn,
								 testCipherSpec_t cipherSuite,
								 sslSessionId_t *sid);
								 
static int32 initializeResumedHandshake(sslConn_t *clnConn, sslConn_t *svrConn,
								testCipherSpec_t cipherSuite);
								
#ifdef SSL_REHANDSHAKES_ENABLED								
static int32 initializeReHandshake(sslConn_t *clnConn, sslConn_t *svrConn,
								int32 cipherSuite);

static int32 initializeResumedReHandshake(sslConn_t *clnConn,
								sslConn_t *svrConn, int32 cipherSuite);
static int32 initializeServerInitiatedReHandshake(sslConn_t *clnConn,
								sslConn_t *svrConn, int32 cipherSuite);
static int32 initializeServerInitiatedResumedReHandshake(sslConn_t *clnConn,
								sslConn_t *svrConn, int32 cipherSuite);	
static int32 initializeUpgradeCertCbackReHandshake(sslConn_t *clnConn,
								sslConn_t *svrConn, int32 cipherSuite);
static int32 initializeUpgradeKeysReHandshake(sslConn_t *clnConn,
								sslConn_t *svrConn, int32 cipherSuite);
static int32 initializeChangeCipherReHandshake(sslConn_t *clnConn,
								sslConn_t *svrConn, int32 cipherSuite);											
#ifdef USE_CLIENT_AUTH
static int32 initializeReHandshakeClientAuth(sslConn_t *clnConn,
						sslConn_t *svrConn, int32 cipherSuite);
#endif /* USE_CLIENT_AUTH */
#endif /* SSL_REHANDSHAKES_ENABLED */
															
#ifdef USE_CLIENT_AUTH
static int32 initializeClientAuthHandshake(sslConn_t *clnConn,
					sslConn_t *svrConn, uint32 cipherSuite,
					sslSessionId_t *sid);
#endif /* USE_CLIENT_AUTH */
								
static int32 performHandshake(sslConn_t *sendingSide, sslConn_t *receivingSide);
static int32 exchangeAppData(sslConn_t *sendingSide, sslConn_t *receivingSide);

/*
	Client-authentication.  Callback that is registered to receive client
	certificate information for custom validation
*/
static int32 clnCertChecker(ssl_t *ssl, psX509Cert_t *cert, int32 alert);

#ifdef SSL_REHANDSHAKES_ENABLED 
static int32 clnCertCheckerUpdate(ssl_t *ssl, psX509Cert_t *cert, int32 alert);
#endif

#ifdef USE_CLIENT_AUTH
static int32 svrCertChecker(ssl_t *ssl, psX509Cert_t *cert, int32 alert);
#endif /* USE_CLIENT_AUTH */

static testCipherSpec_t	ciphers[] = {

#ifdef USE_SSL_RSA_WITH_NULL_SHA
	{"SSL_RSA_WITH_NULL_SHA",
		SSL_RSA_WITH_NULL_SHA,
		1,			/* RSA keys? */
		0,			/* DH keys */
		0,			/* PSK */
		0			/* ECDH keys */
	},
#endif

#ifdef USE_SSL_RSA_WITH_NULL_MD5	
	{"SSL_RSA_WITH_NULL_MD5",
		SSL_RSA_WITH_NULL_MD5,
		1,			/* RSA keys? */
		0,			/* DH keys */
		0,			/* PSK */
		0			/* ECDH keys */
	},
#endif

#ifdef USE_TLS_RSA_WITH_AES_256_CBC_SHA
	{"TLS_RSA_WITH_AES_256_CBC_SHA",
		TLS_RSA_WITH_AES_256_CBC_SHA,
		1,			/* RSA keys? */
		0,			/* DH keys */
		0,			/* PSK */
		0			/* ECDH keys */
	},
#endif


#ifdef USE_TLS_RSA_WITH_AES_128_CBC_SHA
	{"TLS_RSA_WITH_AES_128_CBC_SHA",
		TLS_RSA_WITH_AES_128_CBC_SHA,
		1,			/* RSA keys? */
		0,			/* DH keys */
		0,			/* PSK */
		0			/* ECDH keys */
	},
#endif

#ifdef USE_SSL_RSA_WITH_3DES_EDE_CBC_SHA
	{"SSL_RSA_WITH_3DES_EDE_CBC_SHA",
		SSL_RSA_WITH_3DES_EDE_CBC_SHA,
		1,			/* RSA keys? */
		0,			/* DH keys */
		0,			/* PSK */
		0			/* ECDH keys */
	},
#endif

#ifdef USE_SSL_RSA_WITH_RC4_128_SHA	
	{"SSL_RSA_WITH_RC4_128_SHA",
		SSL_RSA_WITH_RC4_128_SHA,
		1,			/* RSA keys? */
		0,			/* DH keys */
		0,			/* PSK */
		0			/* ECDH keys */
	},
#endif

#ifdef USE_SSL_RSA_WITH_RC4_128_MD5	
	{"SSL_RSA_WITH_RC4_128_MD5",
		SSL_RSA_WITH_RC4_128_MD5,
		1,			/* RSA keys? */
		0,			/* DH keys */
		0,			/* PSK */
		0			/* ECDH keys */
	},
#endif
	
	
	{"NULL", -1, 0, 0, 0, 0} /* must be last */
};

/******************************************************************************/
/*
	This test application will exercise the SSL/TLS handshake and app
	data exchange for every eligible cipher.  
*/

int main(int argc, char **argv)
{
	int32			id;
	sslConn_t		*svrConn, *clnConn;
#ifdef ENABLE_PERF_TIMING
	int32			perfIter;
	uint32			clnTime, svrTime;
#endif /* ENABLE_PERF_TIMING */

	if (matrixSslOpen() < 0) {
		fprintf(stderr, "matrixSslOpen failed, exiting...");
	}

	svrConn = psMalloc(MATRIX_NO_POOL, sizeof(sslConn_t));
	clnConn = psMalloc(MATRIX_NO_POOL, sizeof(sslConn_t));
	memset(svrConn, 0, sizeof(sslConn_t));
	memset(clnConn, 0, sizeof(sslConn_t));
	
	for (id = 0; ciphers[id].cipherId > 0; id++) {
		matrixSslNewSessionId(&clientSessionId);
		_psTraceStr("Testing %s suite\n", ciphers[id].name);
/*
		Standard Handshake
*/
		_psTrace("	Standard handshake test\n");
#ifdef ENABLE_PERF_TIMING
/*
		Each matrixSsl call in the handshake is wrapped by a timer.  Data
		exchange is NOT included in the timer
*/
		clnTime = svrTime = 0;
		_psTraceInt("		%d connections\n", (int32)CONN_ITER);
		for (perfIter = 0; perfIter < CONN_ITER; perfIter++) {
#endif /* ENABLE_PERF_TIMING */		
		if (initializeHandshake(clnConn, svrConn, ciphers[id],
				clientSessionId) < 0) {
			_psTrace("		FAILED: initializing Standard handshake\n");
			goto LBL_FREE;
		}
		if (performHandshake(clnConn, svrConn) < 0) {
			_psTrace("		FAILED: Standard handshake\n");
			goto LBL_FREE;
		} else {
			testTrace("		PASSED: Standard handshake");
			if (exchangeAppData(clnConn, svrConn) < 0 ||
					exchangeAppData(svrConn, clnConn) < 0) {
				_psTrace(" but FAILED to exchange srv application data\n");
			} else {
				testTrace("\n");
			}
		}
#ifdef ENABLE_PERF_TIMING
		svrTime += svrConn->runningTime;
		clnTime += clnConn->runningTime;
		/* Have to reset conn for full handshake... except last time through */
		if (perfIter + 1 != CONN_ITER) {
			matrixSslDeleteSession(clnConn->ssl);
			matrixSslDeleteSession(svrConn->ssl);
			matrixSslClearSessionId(clientSessionId);
		}
		} /* iteration loop close */
		_psTraceInt("		CLIENT:	%d " TIME_UNITS, (int32)clnTime/CONN_ITER);
		_psTraceInt("		SERVER:	%d " TIME_UNITS, (int32)svrTime/CONN_ITER);
		_psTrace("\n");
#endif /* ENABLE_PERF_TIMING */
		
#ifdef SSL_REHANDSHAKES_ENABLED	
/*
		 Re-Handshake (full handshake over existing connection)
*/		
		testTrace("	Re-handshake test (client-initiated)\n");		
		if (initializeReHandshake(clnConn, svrConn, ciphers[id].cipherId) < 0) {
			_psTrace("		FAILED: initializing Re-handshake\n");
			goto LBL_FREE;
		}
		if (performHandshake(clnConn, svrConn) < 0) {
			_psTrace("		FAILED: Re-handshake\n");
			goto LBL_FREE;
		} else {
			testTrace("		PASSED: Re-handshake");			
			if (exchangeAppData(clnConn, svrConn) < 0 ||
					exchangeAppData(svrConn, clnConn) < 0) {
				_psTrace(" but FAILED to exchange application data\n");
			} else {
				testTrace("\n");
			}
		}	
#else
		_psTrace("	Re-handshake tests are disabled (ENABLE_SECURE_REHANDSHAKES)\n");
#endif
				
/*
		Resumed handshake (fast handshake over new connection)
*/				
		testTrace("	Resumed handshake test (new connection)\n");		
		if (initializeResumedHandshake(clnConn, svrConn,
				ciphers[id]) < 0) {
			_psTrace("		FAILED: initializing Resumed handshake\n");
			goto LBL_FREE;
		}
		if (performHandshake(clnConn, svrConn) < 0) {
			_psTrace("		FAILED: Resumed handshake\n");
			goto LBL_FREE;
		} else {
			testTrace("		PASSED: Resumed handshake");
			if (exchangeAppData(clnConn, svrConn) < 0 ||
					exchangeAppData(svrConn, clnConn) < 0) {
				_psTrace(" but FAILED to exchange application data\n");
			} else {
				testTrace("\n");
			}
		}	
		
#ifdef SSL_REHANDSHAKES_ENABLED		
/*
		 Re-handshake initiated by server (full handshake over existing conn)
*/			
		testTrace("	Re-handshake test (server initiated)\n");
		if (initializeServerInitiatedReHandshake(clnConn, svrConn,
									   ciphers[id].cipherId) < 0) {
			_psTrace("		FAILED: initializing Re-handshake\n");
			goto LBL_FREE;
		}
		if (performHandshake(svrConn, clnConn) < 0) {
			_psTrace("		FAILED: Re-handshake\n");
			goto LBL_FREE;
		} else {
			testTrace("		PASSED: Re-handshake");
			if (exchangeAppData(clnConn, svrConn) < 0 ||
					exchangeAppData(svrConn, clnConn) < 0) {
				_psTrace(" but FAILED to exchange application data\n");
			} else {
				testTrace("\n");
			}
		}	
	
		/* Testing 5 more re-handshake paths.  Add some credits */
		matrixSslAddRehandshakeCredits(svrConn->ssl, 5);
/*
		Resumed re-handshake (fast handshake over existing connection)
*/				
		testTrace("	Resumed Re-handshake test (client initiated)\n");
		if (initializeResumedReHandshake(clnConn, svrConn,
				 ciphers[id].cipherId) < 0) {
				_psTrace("		FAILED: initializing Resumed Re-handshake\n");
			goto LBL_FREE;
		}
		if (performHandshake(clnConn, svrConn) < 0) {
			_psTrace("		FAILED: Resumed Re-handshake\n");
			goto LBL_FREE;
		} else {
			testTrace("		PASSED: Resumed Re-handshake");
			if (exchangeAppData(clnConn, svrConn) < 0 ||
					exchangeAppData(svrConn, clnConn) < 0) {
				_psTrace(" but FAILED to exchange application data\n");
			} else {
				testTrace("\n");
			}
		}
		
/*
		 Resumed re-handshake initiated by server (fast handshake over conn)
*/		
		testTrace("	Resumed Re-handshake test (server initiated)\n");
		if (initializeServerInitiatedResumedReHandshake(clnConn, svrConn,
									   ciphers[id].cipherId) < 0) {
				_psTrace("		FAILED: initializing Resumed Re-handshake\n");
			goto LBL_FREE;
		}
		if (performHandshake(svrConn, clnConn) < 0) {
			_psTrace("		FAILED: Resumed Re-handshake\n");
			goto LBL_FREE;
		} else {
			testTrace("		PASSED: Resumed Re-handshake");
			if (exchangeAppData(clnConn, svrConn) < 0 ||
					exchangeAppData(svrConn, clnConn) < 0) {
				_psTrace(" but FAILED to exchange application data\n");
			} else {
				testTrace("\n");
			}
		}		
/*
		Re-handshaking with "upgraded" parameters
*/
		testTrace("	Change cert callback Re-handshake test\n");
		if (initializeUpgradeCertCbackReHandshake(clnConn, svrConn,
									   ciphers[id].cipherId) < 0) {
				_psTrace("		FAILED: init upgrade certCback Re-handshake\n");
			goto LBL_FREE;
		}
		if (performHandshake(clnConn, svrConn) < 0) {
			_psTrace("		FAILED: Upgrade cert callback Re-handshake\n");
			goto LBL_FREE;
		} else {
			testTrace("		PASSED: Upgrade cert callback Re-handshake");
			if (exchangeAppData(clnConn, svrConn) < 0 ||
					exchangeAppData(svrConn, clnConn) < 0) {
				_psTrace(" but FAILED to exchange application data\n");
			} else {
				testTrace("\n");
			}
		}		
/*
		Upgraded keys
*/
		testTrace("	Change keys Re-handshake test\n");
		if (initializeUpgradeKeysReHandshake(clnConn, svrConn,
									   ciphers[id].cipherId) < 0) {
				_psTrace("		FAILED: init upgrade keys Re-handshake\n");
			goto LBL_FREE;
		}
		if (performHandshake(clnConn, svrConn) < 0) {
			_psTrace("		FAILED: Upgrade keys Re-handshake\n");
			goto LBL_FREE;
		} else {
			testTrace("		PASSED: Upgrade keys Re-handshake");
			if (exchangeAppData(clnConn, svrConn) < 0 ||
					exchangeAppData(svrConn, clnConn) < 0) {
				_psTrace(" but FAILED to exchange application data\n");
			} else {
				testTrace("\n");
			}
		}
/*
		Change cipher spec test.  Changing to a hardcoded RSA suite so this
		will not work on suites that don't have RSA material loaded
*/
		if (ciphers[id].rsa == 1 && ciphers[id].ecdh == 0) {
			testTrace("	Change cipher suite Re-handshake test\n");
			if (initializeChangeCipherReHandshake(clnConn, svrConn,
									   ciphers[id].cipherId) < 0) {
					_psTrace("		FAILED: init change cipher Re-handshake\n");
				goto LBL_FREE;
			}
			if (performHandshake(clnConn, svrConn) < 0) {
				_psTrace("		FAILED: Change cipher suite Re-handshake\n");
				goto LBL_FREE;
			} else {
				testTrace("		PASSED: Change cipher suite Re-handshake");
				if (exchangeAppData(clnConn, svrConn) < 0 ||
					exchangeAppData(svrConn, clnConn) < 0) {
					_psTrace(" but FAILED to exchange application data\n");
				} else {
					testTrace("\n");
				}
			}
		}
#endif /* !SSL_REHANDSHAKES_ENABLED */

#ifdef USE_CLIENT_AUTH		
/*
		Client Authentication handshakes
*/				
		if (ciphers[id].psk == 0) {
			
			_psTrace("	Standard Client Authentication test\n");
#ifdef ENABLE_PERF_TIMING
			clnTime = svrTime = 0;
			_psTraceInt("		%d connections\n", (int32)CONN_ITER);
			for (perfIter = 0; perfIter < CONN_ITER; perfIter++) {
#endif /* ENABLE_PERF_TIMING */					
			matrixSslClearSessionId(clientSessionId);
			if (initializeClientAuthHandshake(clnConn, svrConn,
					ciphers[id].cipherId, clientSessionId) < 0) {
				_psTrace("		FAILED: initializing Standard Client Auth handshake\n");
				goto LBL_FREE;
			}
			if (performHandshake(clnConn, svrConn) < 0) {
				_psTrace("		FAILED: Standard Client Auth handshake\n");
				goto LBL_FREE;
			} else {
				testTrace("		PASSED: Standard Client Auth handshake");
				if (exchangeAppData(clnConn, svrConn) < 0 ||
						exchangeAppData(svrConn, clnConn) < 0) {
					_psTrace(" but FAILED to exchange application data\n");
				} else {
					testTrace("\n");
				}
			}
#ifdef ENABLE_PERF_TIMING
			svrTime += svrConn->runningTime;
			clnTime += clnConn->runningTime;
			} /* iteration loop */
			_psTraceInt("		CLIENT:	%d " TIME_UNITS, (int32)clnTime/CONN_ITER);
			_psTraceInt("		SERVER:	%d " TIME_UNITS, (int32)svrTime/CONN_ITER);
			_psTrace("\n==========\n");
#endif /* ENABLE_PERF_TIMING */	
			
			
			testTrace("	Resumed client authentication test\n");
			if (initializeResumedHandshake(clnConn, svrConn, ciphers[id]) < 0) {
				_psTrace("		FAILED: initializing resumed Client Auth handshake\n");
				goto LBL_FREE;
			}
			if (performHandshake(clnConn, svrConn) < 0) {
				_psTrace("		FAILED: Resumed Client Auth handshake\n");
				goto LBL_FREE;
			} else {
				testTrace("		PASSED: Resumed Client Auth handshake");
				if (exchangeAppData(clnConn, svrConn) < 0 ||
					exchangeAppData(svrConn, clnConn) < 0) {
					_psTrace(" but FAILED to exchange application data\n");
				} else {
					testTrace("\n");
				}
			}
#ifdef SSL_REHANDSHAKES_ENABLED	
			testTrace("	Rehandshake adding client authentication test\n");
			if (initializeReHandshakeClientAuth(clnConn, svrConn,
					ciphers[id].cipherId) < 0) {
				_psTrace("		FAILED: initializing reshandshke Client Auth handshake\n");
				goto LBL_FREE;
			}
			/* Must be server initiatated if client auth is being turned on */
			if (performHandshake(svrConn, clnConn) < 0) {
				_psTrace("		FAILED: Rehandshake Client Auth handshake\n");
				goto LBL_FREE;
			} else {
				testTrace("		PASSED: Rehandshake Client Auth handshake");
				if (exchangeAppData(clnConn, svrConn) < 0 ||
					exchangeAppData(svrConn, clnConn) < 0) {
					_psTrace(" but FAILED to exchange application data\n");
				} else {
					testTrace("\n");
				}
			}
#endif /* SSL_REHANDSHAKES_ENABLED */	
		}
#endif /* USE_CLIENT_AUTH */	

LBL_FREE:
		freeSessionAndConnection(svrConn);
		freeSessionAndConnection(clnConn);
	}
	psFree(svrConn);
	psFree(clnConn);
	matrixSslDeleteSessionId(clientSessionId);
	matrixSslClose();

#ifdef WIN32
	_psTrace("Press any key to close");
	getchar();
#endif

	return PS_SUCCESS;	
}

static int32 initializeHandshake(sslConn_t *clnConn, sslConn_t *svrConn,
							testCipherSpec_t cipherSuite, sslSessionId_t *sid)
{
	int32	rc;
	
	if ((rc = initializeServer(svrConn, cipherSuite)) < 0) {
		return rc;
	}
	return initializeClient(clnConn, cipherSuite, sid);

}

#ifdef SSL_REHANDSHAKES_ENABLED
static int32 initializeReHandshake(sslConn_t *clnConn, sslConn_t *svrConn,
								   int32 cipherSuite)
{
	return matrixSslEncodeRehandshake(clnConn->ssl, NULL, NULL,
		SSL_OPTION_FULL_HANDSHAKE, cipherSuite);
}

static int32 initializeServerInitiatedReHandshake(sslConn_t *clnConn,
								sslConn_t *svrConn, int32 cipherSuite)
{
	return matrixSslEncodeRehandshake(svrConn->ssl, NULL, NULL,
		SSL_OPTION_FULL_HANDSHAKE, cipherSuite);
}

static int32 initializeServerInitiatedResumedReHandshake(sslConn_t *clnConn,
								sslConn_t *svrConn, int32 cipherSuite)
{
	return matrixSslEncodeRehandshake(svrConn->ssl, NULL, NULL, 0, cipherSuite);

}

static int32 initializeResumedReHandshake(sslConn_t *clnConn,
							sslConn_t *svrConn, int32 cipherSuite)
{
	return matrixSslEncodeRehandshake(clnConn->ssl, NULL, NULL, 0, cipherSuite);
}

static int32 initializeUpgradeCertCbackReHandshake(sslConn_t *clnConn,
							sslConn_t *svrConn, int32 cipherSuite)
{
	return matrixSslEncodeRehandshake(clnConn->ssl, NULL, clnCertCheckerUpdate,
							0, cipherSuite);
}

static int32 initializeUpgradeKeysReHandshake(sslConn_t *clnConn,
							sslConn_t *svrConn, int32 cipherSuite)
{
/*
	Not really changing the keys but this still tests that passing a
	valid arg will force a full handshake
*/
	return matrixSslEncodeRehandshake(clnConn->ssl, clnConn->ssl->keys, NULL,
							0, cipherSuite);
}

static int32 initializeChangeCipherReHandshake(sslConn_t *clnConn,
							sslConn_t *svrConn, int32 cipherSuite)
{
/*
	Just picking the most common suite
*/
#ifdef USE_SSL_RSA_WITH_3DES_EDE_CBC_SHA
	return matrixSslEncodeRehandshake(clnConn->ssl, NULL, NULL,
					0, SSL_RSA_WITH_3DES_EDE_CBC_SHA);
#else
	return matrixSslEncodeRehandshake(clnConn->ssl, NULL, NULL, 0, 0);
#endif
}

#ifdef USE_CLIENT_AUTH
static int32 initializeReHandshakeClientAuth(sslConn_t *clnConn,
						sslConn_t *svrConn, int32 cipherSuite)
{
	return matrixSslEncodeRehandshake(svrConn->ssl, NULL, svrCertChecker, 0,
			cipherSuite);
}
#endif /* USE_CLIENT_AUTH */
#endif /* SSL_REHANDSHAKES_ENABLED */

static int32 initializeResumedHandshake(sslConn_t *clnConn, sslConn_t *svrConn,
										testCipherSpec_t cipherSuite)
{
	sslSessionId_t	*sessionId;
#ifdef ENABLE_PERF_TIMING
	psTime_t		start, end;
#endif /* ENABLE_PERF_TIMING */	
	
	sessionId = clnConn->ssl->sid;
	
	matrixSslDeleteSession(clnConn->ssl);

#ifdef ENABLE_PERF_TIMING
	clnConn->runningTime = 0;
	psGetTime(&start);
#endif /* ENABLE_PERF_TIMING */	
	if (matrixSslNewClientSession(&clnConn->ssl, clnConn->keys, sessionId,
			cipherSuite.cipherId, clnCertChecker, NULL, NULL, newSessionFlag)
			< 0) {
        return PS_FAILURE;
    }
#ifdef ENABLE_PERF_TIMING
	psGetTime(&end);
	clnConn->runningTime += psDiffMsecs(start, end);
#endif /* ENABLE_PERF_TIMING */
	
	matrixSslDeleteSession(svrConn->ssl);
#ifdef ENABLE_PERF_TIMING
	svrConn->runningTime = 0;
	psGetTime(&start);
#endif /* ENABLE_PERF_TIMING */		
	if (matrixSslNewServerSession(&svrConn->ssl, svrConn->keys, NULL,
			newSessionFlag) < 0) {
        return PS_FAILURE;
    }
#ifdef ENABLE_PERF_TIMING
	psGetTime(&end);
	svrConn->runningTime += psDiffMsecs(start, end);
#endif /* ENABLE_PERF_TIMING */	
	return PS_SUCCESS;
}

#ifdef USE_CLIENT_AUTH
static int32 initializeClientAuthHandshake(sslConn_t *clnConn,
					sslConn_t *svrConn, uint32 cipherSuite, sslSessionId_t *sid)
{
#ifdef ENABLE_PERF_TIMING
	psTime_t		start, end;
#endif /* ENABLE_PERF_TIMING */	

	matrixSslDeleteSession(clnConn->ssl);
#ifdef ENABLE_PERF_TIMING
	clnConn->runningTime = 0;
	psGetTime(&start);
#endif /* ENABLE_PERF_TIMING */	
	if (matrixSslNewClientSession(&clnConn->ssl, clnConn->keys, sid,
			cipherSuite, clnCertChecker, NULL, NULL, newSessionFlag) < 0) {
        return PS_FAILURE;
    }
#ifdef ENABLE_PERF_TIMING
	psGetTime(&end);
	clnConn->runningTime += psDiffMsecs(start, end);
#endif /* ENABLE_PERF_TIMING */
	
	matrixSslDeleteSession(svrConn->ssl);
#ifdef ENABLE_PERF_TIMING
	svrConn->runningTime = 0;
	psGetTime(&start);
#endif /* ENABLE_PERF_TIMING */		
	if (matrixSslNewServerSession(&svrConn->ssl, svrConn->keys, svrCertChecker,
			newSessionFlag)	< 0) {
        return PS_FAILURE;
    }
#ifdef ENABLE_PERF_TIMING
	psGetTime(&end);
	svrConn->runningTime += psDiffMsecs(start, end);
#endif /* ENABLE_PERF_TIMING */		
	return PS_SUCCESS;
}
#endif /* USE_CLIENT_AUTH */

/*
	Recursive handshake
*/
static int32 performHandshake(sslConn_t *sendingSide, sslConn_t *receivingSide)
{
	unsigned char	*inbuf, *outbuf, *plaintextBuf;
	int32			inbufLen, outbufLen, rc, sdrc, dataSent;
	uint32			ptLen;
#ifdef ENABLE_PERF_TIMING	
	psTime_t		start, end;
#endif /* ENABLE_PERF_TIMING */	
	
/*
	Sending side will have outdata ready
*/
#ifdef ENABLE_PERF_TIMING
	psGetTime(&start);
#endif /* ENABLE_PERF_TIMING */	
	outbufLen = matrixSslGetOutdata(sendingSide->ssl, &outbuf);
#ifdef ENABLE_PERF_TIMING	
	psGetTime(&end);
	sendingSide->runningTime += psDiffMsecs(start, end);
#endif /* ENABLE_PERF_TIMING */	
	
/*
	Receiving side must ask for storage space to receive data into
*/
#ifdef ENABLE_PERF_TIMING
	psGetTime(&start);
#endif /* ENABLE_PERF_TIMING */	
	inbufLen = matrixSslGetReadbuf(receivingSide->ssl, &inbuf);
#ifdef ENABLE_PERF_TIMING	
	psGetTime(&end);
	receivingSide->runningTime += psDiffMsecs(start, end);
#endif /* ENABLE_PERF_TIMING */	
	
/*
	The indata is the outdata from the sending side.  copy it over
*/
	dataSent = min(outbufLen, inbufLen);
	memcpy(inbuf, outbuf, dataSent);
	
/*
	Now update the sending side that data has been "sent"
*/
#ifdef ENABLE_PERF_TIMING
	psGetTime(&start);
#endif /* ENABLE_PERF_TIMING */	
	sdrc = matrixSslSentData(sendingSide->ssl, dataSent);
#ifdef ENABLE_PERF_TIMING	
	psGetTime(&end);
	sendingSide->runningTime += psDiffMsecs(start, end);
#endif /* ENABLE_PERF_TIMING */

/*
	Received data
*/
#ifdef ENABLE_PERF_TIMING
	psGetTime(&start);
#endif /* ENABLE_PERF_TIMING */	
	rc = matrixSslReceivedData(receivingSide->ssl, dataSent, &plaintextBuf,
		&ptLen);
#ifdef ENABLE_PERF_TIMING	
	psGetTime(&end);
	receivingSide->runningTime += psDiffMsecs(start, end);
#endif /* ENABLE_PERF_TIMING */	
		
	if (rc == MATRIXSSL_REQUEST_SEND) {
/*
		Success case.  Switch roles and continue
*/
		return performHandshake(receivingSide, sendingSide);
		
	} else if (rc == MATRIXSSL_REQUEST_RECV) {
/*
		This pass didn't take care of it all.  Don't switch roles and
		try again
*/
		return performHandshake(sendingSide, receivingSide);
		
	} else if (rc == MATRIXSSL_HANDSHAKE_COMPLETE) {
		return PS_SUCCESS;

	} else if (rc == MATRIXSSL_RECEIVED_ALERT) {
/*
		Just continue if warning level alert
*/
		if (plaintextBuf[0] == SSL_ALERT_LEVEL_WARNING) {
			if (matrixSslProcessedData(receivingSide->ssl, &plaintextBuf,
					&ptLen) != 0) {
				return PS_FAILURE;	
			}
			return performHandshake(sendingSide, receivingSide);
		} else {
			return PS_FAILURE;
		}
	
	} else {
		printf("Unexpected error in performHandshake: %d\n", rc);
		return PS_FAILURE;
	}
	return PS_FAILURE; /* can't get here */
}

/*
	return 0 on successful encryption/decryption communication
	return -1 on failed comm
*/
static int32 exchangeAppData(sslConn_t *sendingSide, sslConn_t *receivingSide)
{
	int32			writeBufLen, inBufLen, dataSent, rc, sentRc;
	uint32			ptLen, totalProcessed, requestedLen, copyLen, halfReqLen;
	unsigned char	*writeBuf, *inBuf, *plaintextBuf;
	unsigned char	copyByte;

/*
	Client sends the data
*/
	requestedLen = APP_DATA_LEN;
	copyByte = 0x0;	

/*
	Split the data into two records sends.  Exercises the API a bit more 
	having the extra buffer management for the second record
*/
	while (requestedLen > 1) {
		copyByte++;
		halfReqLen = requestedLen / 2;
		writeBufLen = matrixSslGetWritebuf(sendingSide->ssl, &writeBuf,
			halfReqLen);
		
		copyLen = min(halfReqLen, writeBufLen);	
		memset(writeBuf, copyByte, copyLen);
		requestedLen -= copyLen;
		//psTraceBytes("sending", writeBuf, copyLen);

		writeBufLen = matrixSslEncodeWritebuf(sendingSide->ssl, copyLen);
		
		copyByte++;
		writeBufLen = matrixSslGetWritebuf(sendingSide->ssl, &writeBuf,
			halfReqLen);
		
		copyLen = min(halfReqLen, writeBufLen);	
		memset(writeBuf, copyByte, copyLen);
		requestedLen -= copyLen;
		//psTraceBytes("sending", writeBuf, copyLen);

		writeBufLen = matrixSslEncodeWritebuf(sendingSide->ssl, copyLen);
	}		

	totalProcessed = 0;	
	
SEND_MORE:		
	writeBufLen = matrixSslGetOutdata(sendingSide->ssl, &writeBuf);	

/*
	Receiving side must ask for storage space to receive data into.
	
	A good optimization of the buffer management can be seen here if a
	second pass was required:  the inBufLen should exactly match the
	writeBufLen because when matrixSslReceivedData was called below the
	record length was parsed off and the buffer was reallocated to the
	exact necessary length
*/
	inBufLen = matrixSslGetReadbuf(receivingSide->ssl, &inBuf);
	
	dataSent = min(writeBufLen, inBufLen);
	memcpy(inBuf, writeBuf, dataSent);

/*
	Now update the sending side that data has been "sent"
*/
	sentRc = matrixSslSentData(sendingSide->ssl, dataSent);
		
/*
	Received data
*/
	rc = matrixSslReceivedData(receivingSide->ssl, dataSent, &plaintextBuf,
		&ptLen);
		
	if (rc == MATRIXSSL_REQUEST_RECV) {
		goto SEND_MORE;
	} else if (rc == MATRIXSSL_APP_DATA) {
		while (rc == MATRIXSSL_APP_DATA) {
			//psTraceBytes("received", plaintextBuf, ptLen);
			if ((rc = matrixSslProcessedData(receivingSide->ssl, &plaintextBuf,
					&ptLen)) != 0) {
				if (rc == MATRIXSSL_APP_DATA) {
					continue;
				} else if (rc == MATRIXSSL_REQUEST_RECV) {
					goto SEND_MORE;
				} else {
					return PS_FAILURE;
				}
			}
		}
		if (sentRc == MATRIXSSL_REQUEST_SEND) {
			goto SEND_MORE;
		}
	} else {
		printf("Unexpected error in exchangeAppData: %d\n", rc);
		return PS_FAILURE;
	}
	
	return PS_SUCCESS;
}


static int32 initializeServer(sslConn_t *conn, testCipherSpec_t cipherSuite)
{
	sslKeys_t	*keys = NULL;
	ssl_t		*ssl = NULL;
#ifdef ENABLE_PERF_TIMING
	psTime_t	start, end;
#endif /* ENABLE_PERF_TIMING */	

	if (conn->keys == NULL) {
		if (matrixSslNewKeys(&keys) < PS_SUCCESS) {
			return PS_MEM_FAIL;
		}
		conn->keys = keys;




		if (cipherSuite.rsa && !cipherSuite.ecdh) {
#if defined(MATRIX_USE_FILE_SYSTEM) && !defined(USE_HEADER_KEYS)	
			if (matrixSslLoadRsaKeys(keys, svrCertFile, svrKeyFile, NULL,
					clnCAfile) < 0) {
				return PS_FAILURE;
			}
#endif /* MATRIX_USE_FILE_SYSTEM && !USE_HEADER_KEYS */

#ifdef USE_HEADER_KEYS
			if (matrixSslLoadRsaKeysMem(keys, RSA1024, sizeof(RSA1024),
						RSA1024KEY, sizeof(RSA1024KEY), RSA2048CA,
						sizeof(RSA2048CA)) < 0) {
				return PS_FAILURE;
			}	
#endif /* USE_HEADER_KEYS */
		}
		
	}
#ifdef ENABLE_PERF_TIMING
	conn->runningTime = 0;
	psGetTime(&start);
#endif /* ENABLE_PERF_TIMING */
/*
	Create a new SSL session for the new socket and register the
	user certificate validator. No client auth first time through
*/
    if (matrixSslNewServerSession(&ssl, conn->keys, NULL, newSessionFlag) < 0) {
        return PS_FAILURE;
    }
	
#ifdef ENABLE_PERF_TIMING
	psGetTime(&end);
	conn->runningTime += psDiffMsecs(start, end);
#endif /* ENABLE_PERF_TIMING */
	conn->ssl = ssl;

	return PS_SUCCESS;
}


static int32 initializeClient(sslConn_t *conn, testCipherSpec_t cipherSuite,
				 sslSessionId_t *sid)
{
	ssl_t		*ssl;
	sslKeys_t	*keys;
#ifdef ENABLE_PERF_TIMING
	psTime_t	start, end;
#endif /* ENABLE_PERF_TIMING */

	if (conn->keys == NULL) {
		if (matrixSslNewKeys(&keys) < PS_SUCCESS) {
			return PS_MEM_FAIL;
		}
		conn->keys = keys;
		

		if (cipherSuite.rsa && !cipherSuite.ecdh) {
#if defined(MATRIX_USE_FILE_SYSTEM) && !defined(USE_HEADER_KEYS)
			if (matrixSslLoadRsaKeys(keys, clnCertFile, clnKeyFile, NULL,
					svrCAfile) < 0) {
				return PS_FAILURE;
			}
#endif /* MATRIX_USE_FILE_SYSTEM && !USE_HEADER_KEYS */

#ifdef USE_HEADER_KEYS
			if (matrixSslLoadRsaKeysMem(keys, RSA2048, sizeof(RSA2048),
					RSA2048KEY, sizeof(RSA2048KEY), RSA1024CA,
					sizeof(RSA1024CA)) < 0) {
				return PS_FAILURE;
			}
#endif /* USE_HEADER_KEYS */
		}
	
	}
	
	conn->ssl = NULL;
#ifdef ENABLE_PERF_TIMING
	conn->runningTime = 0;
	psGetTime(&start);
#endif /* ENABLE_PERF_TIMING */	

    if (matrixSslNewClientSession(&ssl, conn->keys, sid, cipherSuite.cipherId,
			clnCertChecker, NULL, NULL, newSessionFlag) < 0) {
        return PS_FAILURE;
    }
	
#ifdef ENABLE_PERF_TIMING
	psGetTime(&end);
	conn->runningTime += psDiffMsecs(start, end);
#endif /* ENABLE_PERF_TIMING */

	conn->ssl = ssl;
	
	return PS_SUCCESS;
}

/******************************************************************************/
/*
	Delete session and connection
 */
static void freeSessionAndConnection(sslConn_t *conn)
{
	if (conn->ssl != NULL) {
		matrixSslDeleteSession(conn->ssl);
	}
   	matrixSslDeleteKeys(conn->keys);
	conn->ssl = NULL;
	conn->keys = NULL;
}

static int32 clnCertChecker(ssl_t *ssl, psX509Cert_t *cert, int32 alert)
{
	return alert;
}	

#ifdef SSL_REHANDSHAKES_ENABLED
static int32 clnCertCheckerUpdate(ssl_t *ssl, psX509Cert_t *cert, int32 alert)
{
	return alert;
}	
#endif /* SSL_REHANDSHAKES_ENABLED */

#ifdef USE_CLIENT_AUTH
static int32 svrCertChecker(ssl_t *ssl, psX509Cert_t *cert, int32 alert)
{
	return alert;
}
#endif /* USE_CLIENT_AUTH */

/******************************************************************************/


