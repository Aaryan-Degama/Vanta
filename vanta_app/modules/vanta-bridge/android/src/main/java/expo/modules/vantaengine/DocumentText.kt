package expo.modules.vantaengine

import android.graphics.pdf.PdfRenderer
import android.os.Build
import android.os.ParcelFileDescriptor
import android.util.Log
import java.io.File
import java.util.zip.ZipFile

// Text beyond this is not indexed; keeps one huge file from bloating the FTS table.
private const val MAX_CHARS = 200_000

/**
 * Best-effort plain text of a document, or "" when the format is unsupported
 * or the file cannot be read (the document is then searchable by name only).
 *
 * ponytail: .xls (binary) and PDFs below Android 15 are name-only; add a
 * PDF/xls parser library if that matters.
 */
fun extractDocumentText(path: String): String = try {
    val file = File(path)
    when (file.extension.lowercase()) {
        "txt", "md", "csv" -> file.bufferedReader().use { r ->
            val buf = CharArray(MAX_CHARS)
            val n = r.read(buf)
            if (n > 0) String(buf, 0, n) else ""
        }
        "docx" -> zipXmlText(file) { it == "word/document.xml" }
        "pptx" -> zipXmlText(file) { it.startsWith("ppt/slides/slide") && it.endsWith(".xml") }
        "xlsx" -> zipXmlText(file) { it == "xl/sharedStrings.xml" }
        "odt", "ods" -> zipXmlText(file) { it == "content.xml" }
        "rtf" -> rtfText(file.readText().take(MAX_CHARS * 2))
        "pdf" -> if (Build.VERSION.SDK_INT >= 35) pdfText(file) else ""
        else -> ""
    }.take(MAX_CHARS)
} catch (e: Throwable) {
    // Throwable: a corrupt file can raise OutOfMemoryError or zip errors.
    Log.w("VantaDocs", "Text extraction failed for $path", e)
    ""
}

/** Concatenated text of the XML entries of a zip-based office file. */
private fun zipXmlText(file: File, wanted: (String) -> Boolean): String =
    ZipFile(file).use { zip ->
        val sb = StringBuilder()
        for (entry in zip.entries().asSequence().filter { wanted(it.name) }.sortedBy { it.name }) {
            if (sb.length >= MAX_CHARS) break
            val xml = zip.getInputStream(entry).bufferedReader().use { it.readText() }
            sb.append(xmlToText(xml)).append('\n')
        }
        sb.toString()
    }

internal fun xmlToText(xml: String): String = xml
    .replace(Regex("<[^>]+>"), " ")
    .replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"")
    .replace("&apos;", "'").replace("&amp;", "&")
    .replace(Regex("\\s+"), " ")
    .trim()

internal fun rtfText(rtf: String): String = rtf
    .replace(Regex("\\\\'[0-9a-fA-F]{2}"), " ")
    .replace(Regex("\\\\[a-zA-Z]+-?\\d* ?"), " ")
    .replace(Regex("[{}\\\\]"), " ")
    .replace(Regex("\\s+"), " ")
    .trim()

private fun pdfText(file: File): String =
    ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY).use { fd ->
        PdfRenderer(fd).use { pdf ->
            val sb = StringBuilder()
            for (i in 0 until pdf.pageCount) {
                if (sb.length >= MAX_CHARS) break
                pdf.openPage(i).use { page ->
                    page.textContents.forEach { sb.append(it.text).append(' ') }
                }
            }
            sb.toString()
        }
    }
