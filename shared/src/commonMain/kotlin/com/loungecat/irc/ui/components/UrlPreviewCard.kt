package com.loungecat.irc.ui.components

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BrokenImage
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import coil3.PlatformContext
import coil3.compose.AsyncImage
import coil3.request.ImageRequest
import coil3.request.crossfade
import com.loungecat.irc.data.model.UrlPreview
import com.loungecat.irc.ui.theme.AppColors
import com.loungecat.irc.util.openUrl

@Composable
fun UrlPreviewCard(preview: UrlPreview, modifier: Modifier = Modifier) {
    val colors = AppColors.current

    Card(
            modifier =
                    modifier.fillMaxWidth().padding(vertical = 4.dp).clickable {
                        openUrl(preview.url)
                    },
            shape = RoundedCornerShape(8.dp),
            colors = CardDefaults.cardColors(containerColor = colors.currentLine),
            elevation = CardDefaults.cardElevation(defaultElevation = 2.dp)
    ) {
        when {
            preview.isLoading -> LoadingPreview()
            preview.error != null -> ErrorPreview(preview.url, preview.error)
            else -> PreviewContent(preview)
        }
    }
}

@Composable
private fun LoadingPreview() {
    val colors = AppColors.current
    Row(
            modifier = Modifier.fillMaxWidth().padding(12.dp),
            verticalAlignment = Alignment.CenterVertically
    ) {
        CircularProgressIndicator(
                modifier = Modifier.size(24.dp),
                color = colors.purple,
                strokeWidth = 2.dp
        )
        Spacer(modifier = Modifier.width(12.dp))
        Text(
                text = "Loading preview...",
                style = MaterialTheme.typography.bodySmall,
                color = colors.comment
        )
    }
}

@Composable
private fun ErrorPreview(url: String, error: String) {
    val colors = AppColors.current
    Column(modifier = Modifier.fillMaxWidth().padding(12.dp)) {
        Text(
                text = url,
                style = MaterialTheme.typography.bodySmall,
                color = colors.cyan,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
        )
        Spacer(modifier = Modifier.height(4.dp))
        Text(
                text = error,
                style = MaterialTheme.typography.bodySmall,
                color = colors.comment,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
        )
    }
}

@Composable
private fun PreviewContent(preview: UrlPreview) {
    val colors = AppColors.current
    Column(modifier = Modifier.fillMaxWidth()) {
        preview.imageUrl?.let { imageUrl ->
            AsyncImage(
                    model =
                            ImageRequest.Builder(PlatformContext.INSTANCE)
                                    .data(imageUrl)
                                    .crossfade(true)
                                    .build(),
                    contentDescription = "Preview image",
                    modifier =
                            Modifier.fillMaxWidth()
                                    .height(180.dp)
                                    .clip(RoundedCornerShape(topStart = 8.dp, topEnd = 8.dp)),
                    contentScale = ContentScale.Crop
            )
        }

        Column(modifier = Modifier.fillMaxWidth().padding(12.dp)) {
            Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.padding(bottom = 4.dp)
            ) {
                preview.favicon?.let { faviconUrl ->
                    AsyncImage(
                            model =
                                    ImageRequest.Builder(PlatformContext.INSTANCE)
                                            .data(faviconUrl)
                                            .crossfade(true)
                                            .build(),
                            contentDescription = "Site favicon",
                            modifier = Modifier.size(16.dp).clip(RoundedCornerShape(2.dp))
                    )
                    Spacer(modifier = Modifier.width(6.dp))
                }

                preview.siteName?.let { siteName ->
                    Text(
                            text = siteName,
                            style = MaterialTheme.typography.bodySmall,
                            color = colors.comment,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                    )
                }
            }

            preview.title?.let { title ->
                Text(
                        text = title,
                        style = MaterialTheme.typography.titleSmall,
                        color = colors.foreground,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.padding(bottom = 4.dp)
                )
            }

            preview.description?.let { description ->
                Text(
                        text = description,
                        style = MaterialTheme.typography.bodySmall,
                        color = colors.comment,
                        maxLines = 3,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.padding(bottom = 4.dp)
                )
            }

            Text(
                    text = preview.url,
                    style = MaterialTheme.typography.bodySmall,
                    color = colors.cyan,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
            )
        }
    }
}

@Composable
fun CompactUrlPreviewCard(preview: UrlPreview, modifier: Modifier = Modifier) {
    val colors = AppColors.current

    Card(
            modifier =
                    modifier.fillMaxWidth().padding(vertical = 4.dp).clickable {
                        openUrl(preview.url)
                    },
            shape = RoundedCornerShape(8.dp),
            colors = CardDefaults.cardColors(containerColor = colors.currentLine),
            elevation = CardDefaults.cardElevation(defaultElevation = 1.dp)
    ) {
        when {
            preview.isLoading -> CompactLoadingPreview()
            preview.error != null -> CompactErrorPreview(preview.url, preview.error)
            else -> CompactPreviewContent(preview)
        }
    }
}

@Composable
private fun CompactPreviewContent(preview: UrlPreview) {
    val colors = AppColors.current
    Row(
            modifier = Modifier.fillMaxWidth().padding(8.dp),
            verticalAlignment = Alignment.CenterVertically
    ) {
        preview.imageUrl?.let { imageUrl ->
            AsyncImage(
                    model =
                            ImageRequest.Builder(PlatformContext.INSTANCE)
                                    .data(imageUrl)
                                    .crossfade(true)
                                    .build(),
                    contentDescription = "Preview thumbnail",
                    modifier = Modifier.size(60.dp).clip(RoundedCornerShape(4.dp)),
                    contentScale = ContentScale.Crop
            )
            Spacer(modifier = Modifier.width(8.dp))
        }

        Column(modifier = Modifier.weight(1f)) {
            preview.title?.let { title ->
                Text(
                        text = title,
                        style = MaterialTheme.typography.bodyMedium,
                        color = colors.foreground,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                )
            }

            preview.siteName?.let { siteName ->
                Text(
                        text = siteName,
                        style = MaterialTheme.typography.bodySmall,
                        color = colors.comment,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                )
            }
        }
    }
}

@Composable
private fun CompactLoadingPreview() {
    val colors = AppColors.current
    Row(
            modifier = Modifier.fillMaxWidth().padding(8.dp),
            verticalAlignment = Alignment.CenterVertically
    ) {
        CircularProgressIndicator(
                modifier = Modifier.size(16.dp),
                color = colors.purple,
                strokeWidth = 2.dp
        )
        Spacer(modifier = Modifier.width(8.dp))
        Text(
                text = "Loading...",
                style = MaterialTheme.typography.bodySmall,
                color = colors.comment
        )
    }
}

@Composable
private fun CompactErrorPreview(url: String, error: String) {
    val colors = AppColors.current
    Row(
            modifier = Modifier.fillMaxWidth().padding(8.dp),
            verticalAlignment = Alignment.CenterVertically
    ) {
        androidx.compose.material3.Icon(
                imageVector = Icons.Default.BrokenImage,
                contentDescription = "Error",
                tint = colors.red,
                modifier = Modifier.size(16.dp)
        )
        Spacer(modifier = Modifier.width(8.dp))
        Column {
            Text(
                    text = "Preview failed",
                    style = MaterialTheme.typography.bodySmall,
                    color = colors.foreground
            )
            Text(
                    text = url,
                    style = MaterialTheme.typography.bodySmall,
                    color = colors.comment,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
            )
        }
    }
}
