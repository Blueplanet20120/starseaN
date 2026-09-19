// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package app

import features.routing.model.RouteRule
import features.subscription.DefaultSubscriptionGroupId
import features.subscription.DefaultSubscriptionUserAgent

const val DefaultRouteOutboundTag = "proxy"
const val DefaultRouteDomainStrategy = 1

val DefaultSubscriptionGroups = listOf(
    SubscriptionGroupState(
        id = DefaultSubscriptionGroupId,
        name = "默认",
        url = "",
        userAgent = DefaultSubscriptionUserAgent,
        updateInterval = "",
        enabled = true,
        builtIn = true,
    ),
)

val DefaultRouteRules = listOf(
    RouteRule(
        id = 1,
        remarks = "ad_blocker",
        outboundTag = "block",
        domain = listOf("geosite:category-ads-all"),
        port = "",
        protocol = "",
        network = "",
        enabled = false,
    ),
    RouteRule(
        id = 2,
        remarks = "Block-QUIC",
        outboundTag = "block",
        port = "443",
        protocol = "",
        network = "udp",
        enabled = true,
    ),
    RouteRule(
        id = 3,
        remarks = "Direct-BT-P2P",
        outboundTag = "direct",
        port = "",
        protocol = "bittorrent",
        network = "",
        enabled = true,
    ),
    RouteRule(
        id = 4,
        remarks = "Direct-Vendor-CN",
        outboundTag = "direct",
        domain = listOf(
            "geosite:apple-cn",
            "geosite:apple@cn",
            "geosite:microsoft@cn",
            "geosite:steam@cn",
            "geosite:category-games@cn",
        ),
        port = "",
        protocol = "",
        network = "",
        enabled = true,
    ),
    RouteRule(
        id = 5,
        remarks = "Proxy-NonChina-Sites",
        outboundTag = DefaultRouteOutboundTag,
        domain = listOf("geosite:google", "geosite:geolocation-!cn"),
        port = "",
        protocol = "",
        network = "",
        enabled = true,
    ),
    RouteRule(
        id = 6,
        remarks = "Direct-China-Sites",
        outboundTag = "direct",
        domain = listOf("geosite:cn", "geosite:private"),
        port = "",
        protocol = "",
        network = "",
        enabled = true,
    ),
    RouteRule(
        id = 7,
        remarks = "Direct-China-IPs",
        outboundTag = "direct",
        ip = listOf("geoip:cn", "geoip:private"),
        port = "",
        protocol = "",
        network = "",
        enabled = true,
    ),
)
