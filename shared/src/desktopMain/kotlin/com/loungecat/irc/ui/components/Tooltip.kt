package com.loungecat.irc.ui.components

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.TooltipArea
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.loungecat.irc.ui.theme.AppColors

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun AppTooltip(text: String, content: @Composable () -> Unit) {
    val colors = AppColors.current

    TooltipArea(
            tooltip = {
                Text(
                        text = text,
                        color = colors.foreground,
                        fontSize = 12.sp,
                        modifier =
                                Modifier.shadow(4.dp, RoundedCornerShape(4.dp))
                                        .background(
                                                colors.windowBackground.copy(alpha = 0.9f),
                                                RoundedCornerShape(4.dp)
                                        )
                                        .padding(8.dp)
                )
            },
            delayMillis = 600,
            content = content
    )
}
