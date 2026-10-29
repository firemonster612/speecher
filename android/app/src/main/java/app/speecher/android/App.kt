package app.speecher.android

import java.util.concurrent.Executors
import okhttp3.OkHttpClient
import okhttp3.brotli.BrotliInterceptor

// Brotli lets the OAuth requests advertise the same compressions Claude Code's Axios client does
// (br included) and still decode the reply, so the sign-in traffic looks native to Cloudflare.
val sharedHttp = OkHttpClient.Builder().addInterceptor(BrotliInterceptor).build()
val sharedExecutor = Executors.newCachedThreadPool()
