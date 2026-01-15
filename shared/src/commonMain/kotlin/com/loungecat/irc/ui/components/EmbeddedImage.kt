package com.loungecat.irc.ui.components

import androidx.compose.foundation.background
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
import coil3.compose.SubcomposeAsyncImage
import coil3.compose.SubcomposeAsyncImageContent
import coil3.PlatformContext
import coil3.request.ImageRequest
import coil3.request.crossfade
import com.loungecat.irc.ui.theme.AppColors
import com.loungecat.irc.util.openUrl

@Composable
fun EmbeddedImage(
    imageUrl: String,
    modifier: Modifier = Modifier,
    maxHeight: Int = 480,
    maxWidth: Int = 500
) {
    val colors = AppColors.current

    Card(
        modifier = modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp)
            .clickable { openUrl(imageUrl) },
        shape = RoundedCornerShape(8.dp),
        colors = CardDefaults.cardColors(
            containerColor = colors.currentLine
        ),
        elevation = CardDefaults.cardElevation(
            defaultElevation = 2.dp
        )
    ) {
        SubcomposeAsyncImage(
            model = ImageRequest.Builder(PlatformContext.INSTANCE)
                .data(imageUrl)
                .crossfade(true)
                .build(),
            contentDescription = "Embedded image",
            modifier = Modifier
                .widthIn(max = maxWidth.dp)
                .heightIn(max = maxHeight.dp)
                .clip(RoundedCornerShape(8.dp)),
            contentScale = ContentScale.Fit,
            loading = { LoadingImagePlaceholder() },
            error = { ErrorImagePlaceholder(imageUrl) },
            success = { SubcomposeAsyncImageContent() }
        )
    }
}

@Composable
fun CompactEmbeddedImage(
    imageUrl: String,
    modifier: Modifier = Modifier,
    size: Int = 240
) {
    val colors = AppColors.current

    Card(
        modifier = modifier
            .size(size.dp)
            .padding(vertical = 4.dp)
            .clickable { openUrl(imageUrl) },
        shape = RoundedCornerShape(8.dp),
        colors = CardDefaults.cardColors(
            containerColor = colors.currentLine
        ),
        elevation = CardDefaults.cardElevation(
            defaultElevation = 1.dp
        )
    ) {
        SubcomposeAsyncImage(
            model = ImageRequest.Builder(PlatformContext.INSTANCE)
                .data(imageUrl)
                .crossfade(true)
                .build(),
            contentDescription = "Embedded image thumbnail",
            modifier = Modifier
                .fillMaxSize()
                .clip(RoundedCornerShape(8.dp)),
            contentScale = ContentScale.Fit,
            loading = { LoadingImagePlaceholder() },
            error = { ErrorImagePlaceholder(imageUrl, compact = true) },
            success = { SubcomposeAsyncImageContent() }
        )
    }
}

@Composable
private fun LoadingImagePlaceholder() {
    val colors = AppColors.current
    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(colors.currentLine),
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            CircularProgressIndicator(
                modifier = Modifier.size(32.dp),
                color = colors.purple,
                strokeWidth = 3.dp
            )
            Text(
                text = "Loading image...",
                style = MaterialTheme.typography.bodySmall,
                color = colors.comment
            )
        }
    }
}

@Composable
private fun ErrorImagePlaceholder(
    imageUrl: String,
    compact: Boolean = false
) {
    val colors = AppColors.current
    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(colors.currentLine)
            .padding(12.dp),
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Icon(
                imageVector = Icons.Default.BrokenImage,
                contentDescription = "Failed to load",
                tint = colors.red,
                modifier = Modifier.size(if (compact) 32.dp else 48.dp)
            )
            
            if (!compact) {
                Text(
                    text = "Failed to load image",
                    style = MaterialTheme.typography.bodySmall,
                    color = colors.comment
                )
                
                Text(
                    text = imageUrl,
                    style = MaterialTheme.typography.bodySmall,
                    color = colors.cyan,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }
    }
}

@Composable
fun CompactImageRow(
    imageUrls: List<String>,
    modifier: Modifier = Modifier
) {
    val colors = AppColors.current
    Row(
        modifier = modifier,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        imageUrls.take(4).forEach { imageUrl ->
            CompactEmbeddedImage(
                imageUrl = imageUrl,
                size = 160
            )
        }
        if (imageUrls.size > 4) {
            Card(
                modifier = Modifier.size(160.dp),
                shape = RoundedCornerShape(8.dp),
                colors = CardDefaults.cardColors(
                    containerColor = colors.currentLine
                )
            ) {
                Box(
                    modifier = Modifier.fillMaxSize(),
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        text = "+${imageUrls.size - 4}",
                        style = MaterialTheme.typography.titleSmall,
                        color = colors.purple
                    )
                }
            }
        }
    }
}

@Composable
fun ImageGallery(
    imageUrls: List<String>,
    modifier: Modifier = Modifier
) {
    val colors = AppColors.current
    when (imageUrls.size) {
        0 -> {}
        1 -> {
            EmbeddedImage(
                imageUrl = imageUrls[0],
                modifier = modifier
            )
        }
        2 -> {
            Row(
                modifier = modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                imageUrls.forEach { imageUrl ->
                    EmbeddedImage(
                        imageUrl = imageUrl,
                        modifier = Modifier.weight(1f),
                        maxHeight = 180
                    )
                }
            }
        }
        else -> {
            Column(
                modifier = modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                EmbeddedImage(
                    imageUrl = imageUrls[0],
                    maxHeight = 180
                )
                
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    imageUrls.drop(1).take(3).forEach { imageUrl ->
                        CompactEmbeddedImage(
                            imageUrl = imageUrl,
                            modifier = Modifier.weight(1f)
                        )
                    }
                    
                    if (imageUrls.size > 4) {
                        Card(
                            modifier = Modifier
                                .size(240.dp)
                                .weight(1f),
                            shape = RoundedCornerShape(8.dp),
                            colors = CardDefaults.cardColors(
                                containerColor = colors.currentLine
                            )
                        ) {
                            Box(
                                modifier = Modifier.fillMaxSize(),
                                contentAlignment = Alignment.Center
                            ) {
                                Text(
                                    text = "+${imageUrls.size - 4} more",
                                    style = MaterialTheme.typography.titleMedium,
                                    color = colors.purple
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}
