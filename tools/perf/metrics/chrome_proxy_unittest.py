# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import base64
import os
import unittest

from metrics import chrome_proxy
from metrics import network_unittest
from metrics import test_page_measurement_results
from telemetry.unittest import tab_test_case


# Timeline events used in tests.
# An HTML not via proxy.
EVENT_HTML_PROXY = network_unittest.NetworkMetricTest.MakeNetworkTimelineEvent(
    url='http://test.html1',
    response_headers={
        'Content-Type': 'text/html',
        'Content-Length': str(len(network_unittest.HTML_BODY)),
        },
    body=network_unittest.HTML_BODY)

# An HTML via proxy with the deprecated Via header.
EVENT_HTML_PROXY_DEPRECATED_VIA = (
    network_unittest.NetworkMetricTest.MakeNetworkTimelineEvent(
    url='http://test.html2',
    response_headers={
        'Content-Type': 'text/html',
        'Content-Encoding': 'gzip',
        'X-Original-Content-Length': str(len(network_unittest.HTML_BODY)),
        'Via': (chrome_proxy.CHROME_PROXY_VIA_HEADER_DEPRECATED +
                ',other-via'),
        },
    body=network_unittest.HTML_BODY))

# An image via proxy with Via header and it is cached.
EVENT_IMAGE_PROXY_CACHED = (
    network_unittest.NetworkMetricTest.MakeNetworkTimelineEvent(
    url='http://test.image',
    response_headers={
        'Content-Type': 'image/jpeg',
        'Content-Encoding': 'gzip',
        'X-Original-Content-Length': str(network_unittest.IMAGE_OCL),
        'Via': '1.1 ' + chrome_proxy.CHROME_PROXY_VIA_HEADER,
        },
    body=base64.b64encode(network_unittest.IMAGE_BODY),
    base64_encoded_body=True,
    served_from_cache=True))

# An image fetched directly.
EVENT_IMAGE_DIRECT = (
    network_unittest.NetworkMetricTest.MakeNetworkTimelineEvent(
    url='http://test.image',
    response_headers={
        'Content-Type': 'image/jpeg',
        'Content-Encoding': 'gzip',
        },
    body=base64.b64encode(network_unittest.IMAGE_BODY),
    base64_encoded_body=True))

# A safe-browsing malware response.
EVENT_MALWARE_PROXY = (
    network_unittest.NetworkMetricTest.MakeNetworkTimelineEvent(
    url='http://test.malware',
    response_headers={
        'X-Malware-Url': '1',
        'Via': '1.1 ' + chrome_proxy.CHROME_PROXY_VIA_HEADER,
        'Location': 'http://test.malware',
        },
    status=307))


class ChromeProxyMetricTest(unittest.TestCase):

  _test_proxy_info = {}

  def _StubGetProxyInfo(self, info):
    def stub(unused_tab, unused_url=''):  # pylint: disable=W0613
      return ChromeProxyMetricTest._test_proxy_info
    chrome_proxy.GetProxyInfoFromNetworkInternals = stub
    ChromeProxyMetricTest._test_proxy_info = info

  def testChromeProxyResponse(self):
    # An https non-proxy response.
    resp = chrome_proxy.ChromeProxyResponse(
        network_unittest.NetworkMetricTest.MakeNetworkTimelineEvent(
            url='https://test.url',
            response_headers={
                'Content-Type': 'text/html',
                'Content-Length': str(len(network_unittest.HTML_BODY)),
                'Via': 'some other via',
                },
            body=network_unittest.HTML_BODY))
    self.assertFalse(resp.ShouldHaveChromeProxyViaHeader())
    self.assertFalse(resp.HasChromeProxyViaHeader())
    self.assertTrue(resp.IsValidByViaHeader())

    # A proxied JPEG image response
    resp = chrome_proxy.ChromeProxyResponse(
        network_unittest.NetworkMetricTest.MakeNetworkTimelineEvent(
            url='http://test.image',
            response_headers={
                'Content-Type': 'image/jpeg',
                'Content-Encoding': 'gzip',
                'Via': '1.1 ' + chrome_proxy.CHROME_PROXY_VIA_HEADER,
                'X-Original-Content-Length': str(network_unittest.IMAGE_OCL),
                },
            body=base64.b64encode(network_unittest.IMAGE_BODY),
            base64_encoded_body=True))
    self.assertTrue(resp.ShouldHaveChromeProxyViaHeader())
    self.assertTrue(resp.HasChromeProxyViaHeader())
    self.assertTrue(resp.IsValidByViaHeader())

  def testChromeProxyMetricForDataSaving(self):
    metric = chrome_proxy.ChromeProxyMetric()
    events = [
        EVENT_HTML_PROXY,
        EVENT_HTML_PROXY_DEPRECATED_VIA,
        EVENT_IMAGE_PROXY_CACHED,
        EVENT_IMAGE_DIRECT]
    metric.SetEvents(events)

    self.assertTrue(len(events), len(list(metric.IterResponses(None))))
    results = test_page_measurement_results.TestPageMeasurementResults(self)

    metric.AddResultsForDataSaving(None, results)
    results.AssertHasPageSpecificScalarValue('resources_via_proxy', 'count', 2)
    results.AssertHasPageSpecificScalarValue('resources_from_cache', 'count', 1)
    results.AssertHasPageSpecificScalarValue('resources_direct', 'count', 2)

  def testChromeProxyMetricForHeaderValidation(self):
    metric = chrome_proxy.ChromeProxyMetric()
    metric.SetEvents([
        EVENT_HTML_PROXY,
        EVENT_HTML_PROXY_DEPRECATED_VIA,
        EVENT_IMAGE_PROXY_CACHED,
        EVENT_IMAGE_DIRECT])

    results = test_page_measurement_results.TestPageMeasurementResults(self)

    missing_via_exception = False
    try:
      metric.AddResultsForHeaderValidation(None, results)
    except chrome_proxy.ChromeProxyMetricException:
      missing_via_exception = True
    # Only the HTTP image response does not have a valid Via header.
    self.assertTrue(missing_via_exception)

    # Two events with valid Via headers.
    metric.SetEvents([
        EVENT_HTML_PROXY_DEPRECATED_VIA,
        EVENT_IMAGE_PROXY_CACHED])
    metric.AddResultsForHeaderValidation(None, results)
    results.AssertHasPageSpecificScalarValue('checked_via_header', 'count', 2)

  def testChromeProxyMetricForBypass(self):
    metric = chrome_proxy.ChromeProxyMetric()
    metric.SetEvents([
        EVENT_HTML_PROXY,
        EVENT_HTML_PROXY_DEPRECATED_VIA,
        EVENT_IMAGE_PROXY_CACHED,
        EVENT_IMAGE_DIRECT])
    results = test_page_measurement_results.TestPageMeasurementResults(self)

    bypass_exception = False
    try:
      metric.AddResultsForBypass(None, results)
    except chrome_proxy.ChromeProxyMetricException:
      bypass_exception = True
    # Two of the first three events have Via headers.
    self.assertTrue(bypass_exception)

    # Use directly fetched image only. It is treated as bypassed.
    metric.SetEvents([EVENT_IMAGE_DIRECT])
    metric.AddResultsForBypass(None, results)
    results.AssertHasPageSpecificScalarValue('bypass', 'count', 1)

  def testChromeProxyMetricForHTTPFallback(self):
    metric = chrome_proxy.ChromeProxyMetric()
    metric.SetEvents([
        EVENT_HTML_PROXY,
        EVENT_HTML_PROXY_DEPRECATED_VIA])
    results = test_page_measurement_results.TestPageMeasurementResults(self)

    fallback_exception = False
    info = {}
    info['enabled'] = False
    self._StubGetProxyInfo(info)
    try:
      metric.AddResultsForBypass(None, results)
    except chrome_proxy.ChromeProxyMetricException:
      fallback_exception = True
    self.assertTrue(fallback_exception)

    fallback_exception = False
    info['enabled'] = True
    info['proxies'] = [
        'something.else.com:80',
        chrome_proxy.PROXY_SETTING_DIRECT
        ]
    self._StubGetProxyInfo(info)
    try:
      metric.AddResultsForBypass(None, results)
    except chrome_proxy.ChromeProxyMetricException:
      fallback_exception = True
    self.assertTrue(fallback_exception)

    info['enabled'] = True
    info['proxies'] = [
        chrome_proxy.PROXY_SETTING_HTTP,
        chrome_proxy.PROXY_SETTING_DIRECT
        ]
    self._StubGetProxyInfo(info)
    metric.AddResultsForHTTPFallback(None, results)

  def testChromeProxyMetricForSafebrowsing(self):
    metric = chrome_proxy.ChromeProxyMetric()
    metric.SetEvents([EVENT_MALWARE_PROXY])
    results = test_page_measurement_results.TestPageMeasurementResults(self)

    metric.AddResultsForSafebrowsing(None, results)
    results.AssertHasPageSpecificScalarValue('safebrowsing', 'boolean', True)

    # Clear results and metrics to test no response for safebrowsing
    results = test_page_measurement_results.TestPageMeasurementResults(self)
    metric.SetEvents([])
    metric.AddResultsForSafebrowsing(None, results)
    results.AssertHasPageSpecificScalarValue('safebrowsing', 'boolean', True)


_TEST_DIR = os.path.join(os.path.dirname(__file__), 'unittest_data')
_PROXY_SETTING_PAGE = os.path.join(_TEST_DIR,
                                   'chrome_proxy_net_internals_proxy.html')
_TEST_BAD_PROXY_RETRY_TIME = '1397851426335'

class ChromeProxyTabTest(tab_test_case.TabTestCase):
  def __init__(self, *args):
    super(ChromeProxyTabTest, self).__init__(*args)

  def testGetProxyInfoFromNetworkInternals(self):

    """Parse the test chrome://net-internals/#proxy page to test the information
    is extracted correctly.
    """

    if self._tab.browser.platform.GetOSName() != 'android':
      return

    self._browser.SetHTTPServerDirectories(_TEST_DIR)
    info = chrome_proxy.GetProxyInfoFromNetworkInternals(
        self._tab, self._browser.http_server.UrlOf(_PROXY_SETTING_PAGE))
    self.assertTrue(info['enabled'])
    self.assertSequenceEqual(
        ['proxy.googlezip.net:443', 'compress.googlezip.net:80', 'direct://'],
        info['proxies'])
    bad_proxies = info['badProxies']
    self.assertEqual(2, len(bad_proxies))
    for p in bad_proxies:
      self.assertTrue(p['proxy'] in [
          chrome_proxy.PROXY_SETTING_HTTPS,
          chrome_proxy.PROXY_SETTING_HTTP])
      self.assertEqual(_TEST_BAD_PROXY_RETRY_TIME, p['retry'])
