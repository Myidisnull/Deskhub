package com.deskhub.app

import android.content.ContentResolver
import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import android.webkit.MimeTypeMap
import androidx.annotation.RequiresApi
import java.io.File

object ReceivedFiles {
    const val FOLDER_NAME = "Deskhub"
    private const val PART_SUFFIX = ".deskhub-part"

    fun transferDir(context: Context): String =
        File(context.getExternalFilesDir(null) ?: context.filesDir, FOLDER_NAME).absolutePath

    fun exportCompleted(
        context: Context,
        dir: String,
    ): List<String> {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return emptyList()
        val files = File(dir).listFiles() ?: return emptyList()
        val delivered = mutableListOf<String>()
        for (file in files) {
            if (!file.isFile || file.name.endsWith(PART_SUFFIX)) continue
            if (exportOne(context.contentResolver, file)) {
                delivered.add(file.name)
                file.delete()
            }
        }
        return delivered
    }

    @RequiresApi(Build.VERSION_CODES.Q)
    private fun exportOne(
        resolver: ContentResolver,
        file: File,
    ): Boolean {
        val mime = mimeOf(file.name)
        val (collection, folder) = destinationFor(mime)
        val values =
            ContentValues().apply {
                put(MediaStore.MediaColumns.DISPLAY_NAME, file.name)
                put(MediaStore.MediaColumns.MIME_TYPE, mime)
                put(MediaStore.MediaColumns.RELATIVE_PATH, "$folder/$FOLDER_NAME")
                put(MediaStore.MediaColumns.IS_PENDING, 1)
            }
        val uri = resolver.insert(collection, values) ?: return false
        return runCatching {
            copyInto(resolver, file, uri)
            values.clear()
            values.put(MediaStore.MediaColumns.IS_PENDING, 0)
            resolver.update(uri, values, null, null)
            true
        }.getOrElse {
            resolver.delete(uri, null, null)
            false
        }
    }

    @RequiresApi(Build.VERSION_CODES.Q)
    private fun destinationFor(mime: String): Pair<Uri, String> =
        when {
            mime.startsWith("image/") ->
                MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY) to
                    Environment.DIRECTORY_PICTURES
            mime.startsWith("video/") ->
                MediaStore.Video.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY) to
                    Environment.DIRECTORY_MOVIES
            else ->
                MediaStore.Downloads.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY) to
                    Environment.DIRECTORY_DOWNLOADS
        }

    private fun copyInto(
        resolver: ContentResolver,
        file: File,
        uri: Uri,
    ) {
        val stream = resolver.openOutputStream(uri) ?: throw IllegalStateException("no stream for $uri")
        stream.use { out -> file.inputStream().use { it.copyTo(out) } }
    }

    private fun mimeOf(name: String): String {
        val ext = name.substringAfterLast('.', "").lowercase()
        return MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext) ?: "application/octet-stream"
    }
}
