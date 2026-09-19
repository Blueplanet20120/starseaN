// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package features.resources

import android.annotation.SuppressLint
import android.graphics.Color as AndroidColor
import android.view.ViewGroup
import android.webkit.WebResourceRequest
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import org.commonmark.parser.Parser
import org.commonmark.renderer.html.HtmlRenderer
import top.yukonga.miuix.kmp.theme.MiuixTheme

@Composable
internal fun XrayCoreReleaseNotesView(
    notes: String,
    modifier: Modifier = Modifier,
) {
    if (notes.isBlank()) return
    val colors = MiuixTheme.colorScheme
    val uriHandler = LocalUriHandler.current
    val html = remember(notes, colors.onSurface, colors.onSurfaceVariantSummary, colors.primary) {
        wrapReleaseNotesHtml(
            body = renderReleaseNotesHtml(notes),
            textColor = colors.onSurface.toCssColor(),
            mutedColor = colors.onSurfaceVariantSummary.toCssColor(),
            linkColor = colors.primary.toCssColor(),
        )
    }
    AndroidView(
        modifier = modifier
            .fillMaxWidth()
            .heightIn(min = 160.dp, max = 360.dp),
        factory = { context ->
            WebView(context).apply {
                setBackgroundColor(AndroidColor.TRANSPARENT)
                isVerticalScrollBarEnabled = true
                isHorizontalScrollBarEnabled = false
                settings.apply {
                    javaScriptEnabled = false
                    loadsImagesAutomatically = true
                    blockNetworkImage = false
                    mixedContentMode = WebSettings.MIXED_CONTENT_COMPATIBILITY_MODE
                    loadWithOverviewMode = true
                    useWideViewPort = true
                    setSupportZoom(false)
                }
                webViewClient = object : WebViewClient() {
                    override fun shouldOverrideUrlLoading(
                        view: WebView,
                        request: WebResourceRequest,
                    ): Boolean {
                        val url = request.url?.toString().orEmpty()
                        if (url.isBlank() || url.startsWith("data:", ignoreCase = true)) {
                            return false
                        }
                        runCatching { uriHandler.openUri(url) }
                        return true
                    }
                }
                layoutParams = ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT,
                )
            }
        },
        update = { webView ->
            webView.loadDataWithBaseURL(
                GitHubBaseUrl,
                html,
                "text/html",
                Charsets.UTF_8.name(),
                null,
            )
        },
        onRelease = { webView ->
            webView.stopLoading()
            webView.destroy()
        },
    )
}

internal fun renderReleaseNotesHtml(markdown: String): String {
    val document = ReleaseNotesParser.parse(markdown)
    return ReleaseNotesHtmlRenderer.render(document)
}

@SuppressLint("DefaultLocale")
private fun Color.toCssColor(): String {
    val red = (red * 255f + 0.5f).toInt().coerceIn(0, 255)
    val green = (green * 255f + 0.5f).toInt().coerceIn(0, 255)
    val blue = (blue * 255f + 0.5f).toInt().coerceIn(0, 255)
    return if (alpha >= 0.995f) {
        String.format("#%02x%02x%02x", red, green, blue)
    } else {
        String.format("rgba(%d,%d,%d,%.2f)", red, green, blue, alpha)
    }
}

private fun wrapReleaseNotesHtml(
    body: String,
    textColor: String,
    mutedColor: String,
    linkColor: String,
): String {
    return """
        <!DOCTYPE html>
        <html>
        <head>
          <meta charset="utf-8"/>
          <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1"/>
          <style>
            html, body {
              margin: 0;
              padding: 0;
              background: transparent;
              color: $textColor;
              font-family: sans-serif;
              font-size: 14px;
              line-height: 1.55;
              word-wrap: break-word;
              overflow-wrap: anywhere;
            }
            h1, h2, h3, h4 {
              color: $textColor;
              font-size: 15px;
              font-weight: 600;
              margin: 14px 0 8px;
            }
            h1 { font-size: 17px; }
            h2 { font-size: 16px; }
            p, ul, ol { margin: 0 0 10px; }
            ul, ol { padding-left: 1.3em; }
            a { color: $linkColor; text-decoration: none; }
            img {
              max-width: 100%;
              height: auto;
              border-radius: 8px;
              display: block;
              margin: 8px 0;
            }
            code, pre {
              font-family: monospace;
              font-size: 12px;
              background: rgba(127,127,127,0.16);
              border-radius: 6px;
            }
            code { padding: 1px 4px; }
            pre { padding: 10px; overflow-x: auto; }
            pre code { background: transparent; padding: 0; }
            hr {
              border: 0;
              border-top: 1px solid $mutedColor;
              margin: 14px 0;
              opacity: 0.4;
            }
            blockquote {
              margin: 0 0 10px;
              padding-left: 10px;
              border-left: 3px solid $mutedColor;
              color: $mutedColor;
            }
          </style>
        </head>
        <body>$body</body>
        </html>
    """.trimIndent()
}

private const val GitHubBaseUrl = "https://github.com/"
private val ReleaseNotesParser = Parser.builder().build()
private val ReleaseNotesHtmlRenderer = HtmlRenderer.builder().build()
