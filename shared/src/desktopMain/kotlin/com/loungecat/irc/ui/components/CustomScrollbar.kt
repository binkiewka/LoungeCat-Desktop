package com.loungecat.irc.ui.components

import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.ScrollbarStyle
import androidx.compose.foundation.VerticalScrollbar
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.rememberScrollbarAdapter
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.loungecat.irc.ui.theme.AppColors

@Composable
fun CustomVerticalScrollbar(listState: LazyListState, modifier: Modifier = Modifier) {
    val colors = AppColors.current

    VerticalScrollbar(
            modifier = modifier.fillMaxHeight(),
            adapter = rememberScrollbarAdapter(listState),
            style =
                    ScrollbarStyle(
                            minimalHeight = 16.dp,
                            thickness = 8.dp,
                            shape = RoundedCornerShape(4.dp),
                            hoverDurationMillis = 300,
                            unhoverColor = colors.comment.copy(alpha = 0.5f),
                            hoverColor = colors.comment
                    )
    )
}

@Composable
fun CustomVerticalScrollbar(scrollState: ScrollState, modifier: Modifier = Modifier) {
    val colors = AppColors.current

    VerticalScrollbar(
            modifier = modifier.fillMaxHeight(),
            adapter = rememberScrollbarAdapter(scrollState),
            style =
                    ScrollbarStyle(
                            minimalHeight = 16.dp,
                            thickness = 8.dp,
                            shape = RoundedCornerShape(4.dp),
                            hoverDurationMillis = 300,
                            unhoverColor = colors.comment.copy(alpha = 0.5f),
                            hoverColor = colors.comment
                    )
    )
}
