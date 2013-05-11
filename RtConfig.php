<?php
/*
 * Extension to allow RtConfig to work inside MediaWiki
 * by sibbi <at> dot1q.org (http://www.dot1q.org/)
 *
 * Loosely based on the GraphViz plugin for syntax which
 * was based on CoffMan's original GraphViz Extension.
 * License is maintained as the original GNU license
 *
 * License: http://www.gnu.org/copyleft/fdl.html
 *
 * To use this extension please insert the following
 * into LocalSettings.php and copy the extension to
 * extensions/RtConfig/RtConfig.php
 *
 * require_once("extensions/RtConfig/RtConfig.php"); 
 *
 * If you want to configure the settings for this module
 * you will always need the following line in LocalSettings.php as well
 * $wgRtConfigSettings = new RtConfigSettings();
 * And some of these lines, depending on what you want to configure
 * if the lines are not present, reasonable defaults will be used
 * see further below for more information on individual configuration options
 *
 * $wgRtConfigSettings->Debug = true;
 * $wgRtConfigSettings->Command = "/usr/local/bin/RtConfig";
 * $wgRtConfigSettings->IgnoreErrors = false;
 * $wgRtConfigSettings->ReportErrors = false;
 * $wgRtConfigSettings->Files = array();
 * $wgRtConfigSettings->SourceList = "/home/someone/file1,/home/someone/file2";
 * $wgRtConfigSettings->Format = "cisco";
 * $wgRtConfigSettings->WhoisHost="whois.radb.net";
 * $wgRtConfigSettings->WhoisPort=43;
 * $wgRtConfigSettings->WhoisProtocol="irrd";
 * $wgRtConfigSettings->NoMatchIPInbound=false;
 * $wgRtConfigSettings->DisableAccessListCache=false;
 * $wgRtConfigSettings->SupressMartians=false;
 * $wgRtConfigSettings->CiscoNOCompressACLs=false;
 * $wgRtConfigSettings->CiscoUsePrefixLists=false;
 * $wgRtConfigSettings->CiscoEliminateDupMapParts=false;
 * $wgRtConfigSettings->CiscoSkipRouteMaps=false;
 * $wgRtConfigSettings->CiscoForceTilda=false;
 * $wgRtConfigSettings->CiscoEmptyLists=false;
 *
 * Configuration
 *
 * You can override these settings in LocalSettings.php.
 * Configuration must be done AFTER including this extension
 * using:
 * 
 * Configuration options:
 *
 * $wgRtConfigSettings->Command
 *   Where the actual RtConfig binary is on your system
 *   usually (and defaults to) /usr/local/bin/RtConfig
 *
 * $wgRtConfigSettings->Files
 *   An array of paths to IRR cache files you want to 
 *   include.  Defaults to none.  
 *
 * $wgRtConfigSettings->SourceList
 *   A comma seperated string of sources to consider.  If the
 *   same object is defined multiple times, the first match is used
 *   (from left to right), defaults to none,
 *   see RtConfig manual page for more info. 
 * 
 * $wgRtConfigSettings->Format
 *   The configuration format for RtConfig, defaults to
 *   cisco, other options include junos, bcc (for bay), 
 *   gated or rsd, see RtConfig manual page for more info.
 *
 * $wgRtConfigSettings->WhoisHost
 *   The host to establish a whois connection to, the default
 *   is whois.radb.net
 *
 * $wgRtConfigSettings->WhoisPort
 *   The port on the whois server to connect to, the default is 43
 *
 * $wgRtConfigSettings->WhoisProtocol
 *   The protocol to used to establish a connection to the whois host
 *   The default is irrd.  Other options are ripe (bird) and ripe_perl
 *   for the old ripe server, see RtConfig manual page for more info.
 *
 * $wgRtConfigSettings->ReportErrors
 *   Whether to report errors in whois database/parsing , the default is false, 
 *   see RtConfig manual page for more info.
 * 
 * $wgRtConfigSettings->IgnoreErrors
 *   Whether to ignore errors in whois database/parsing, the default is false,
 *   set RtConfig manual page for more info
 *
 * $wgRtConfigSettings->NoMatchIPInbound
 *   Legacy option to support old Cisco Routers which do not suppport ip access-list
 *   matches within route-map, defaults to false, see RtConfig manul page for more info.
 *
 * $wgRtConfigSettings->DisableAccessListCache
 *   By default RtConfig will cache access-lists (and in the future possibly other data
 *   as well, meaning it will re-use the same access-list number instead of generating 
 *   a new list.  Turning this off saves a little memory on the RtConfig server while
 *   using more resources on the router.  Default is false
 *
 * $wgRtConfigSettings->SupressMartians
 *   This option only affects cisco configurations.  All access-lists generated will
 *   deny the standard martians, default is false, see RtConfig manual page for more info.
 *
 * $wgRtConfigSettings->CiscoNoCompressACLs
 *   This option only affects cisco configurations.  Settings this option to true will
 *   disable combining multiple cisco access list lines into a single line using wildcards
 *   whenever possible, defaults to false, see RtConfig manual page for more info.
 *
 * $wgRtConfigSettings->CiscoUsePrefixLists
 *   This option only affects cisco configurations.  The options instructs RtConfig to
 *   output prefix-lists which yield higher performance than access-lists.  This flag
 *   automatically turns on -cisco_compress_acls, settings CiscoNoCompressACLs to true
 *   while also setting this option to true, does not make sense.  Defaults to false,
 *   see RtConfig manual page for more info.
 *
 * $wgRtConfigSettings->CiscoEliminateDupMapParts
 *   This option only affects cisco configuratiom.  The option instructs RtConfig to
 *   eliminate a later map part if it's filter is covered by earlier map parts.  Defaults
 *   to false, see RtConfig for more information
 *
 * $wgRtConfigSettings->CiscoSkipRouteMaps
 *   This option only affects cisco configurations.  The option instruct RtConfig to
 *   not print route-maps, hence it will only print access lists.  Defaults to false,
 *   see RtConfig manual page for more info.
 *
 * $wgRtConfigSettings->CiscoForceTilde
 *   This option only affects cisco configurations.  The option forces * and + operators in
 *   AS-PATH regular expressions to be interpreted as ~* and ~+ operators.  This is useful
 *   if you get as path access-lists with really long lines, since this will force them
 *   to multiple lines.  Defaults to false, See RtConfig manual page for more information.
 *
 * $wgRtConfigSettings->CiscoEmptyLists
 *   This option only affects cisco configurations.  The option forces interpreting ANY/NOT ANY
 *   prefix filter as universal/empty set of prefixes, and produces access lists for them.  Defaults
 *   to false, see RtConfig manual page for more info.
 *
 * $wgRtConfigSettings->Debug
 *   Turns on some debugging output for the extension, defaults to false.
 *
 * WARNING:  This script does NOT perform sanitizing checks on configuration options OR user input, if this is a problem for you
 *           do not use this extension!
 *
 *
 *  Version 1.00 - Initial release

 *  
 *
 */

class RtConfigSettings {
 	public $Command, $Files, $SourceList, $Format, $Debug;
	public $WhoisHost, $WhoisPort, $WhoisProtocol, $ReportErrors;
	public $NoMatchIPInbound, $DisableAccessListCache, $SupressMartians;
	public $CiscoNOCompressACLs, $CiscoUsePrefixLists, $CiscoEliminateDupMapParts;
	public $CiscoSkipRouteMaps, $CiscoForceTilde, $CiscoEmptyLists;
};


// Media Wiki Plugin Stuff
// -----------------------
$wgExtensionFunctions[] = "wfRtConfigExtension";

// Credits and description of the extension

$wgExtensionCredits['parserhook'][] = array(
  'name'=>'RtConfig',
  'author'=>'Sigurbjorn B. Larusson <http://www.dot1q.org>, sibbi <at> dot1q.org',
  'url'=> 'http://www.mediawiki.org/wiki/Extension:RtConfig',
  'description'=>'RtConfig analyzes the routing policies registered in the Internet Routing Registry (IRR) and produces router configuration files.  This extension allows you to keep the RtConfig template files and policies within Wiki for easy access and editing',
  'version'=>'1.0'
);

// Setup a hook on <rtconfig> to launch function parseRtConfig
function wfRtConfigExtension() {
	global $wgParser;
	$wgParser->setHook( "rtconfig", "parseRtConfig" );
}

function parseRtConfig ( $timelinesrc )	
{
	global $wgRtConfigSettings;

	# Build the command line to launch based on the options provided (if any..)

	# The binary to use
	if($wgRtConfigSettings->Command) {
		# Override the default command...
		$cmd = $wgRtConfigSettings->Command . " ";
	} else {
		$cmd = "/usr/local/bin/RtConfig ";
	}

	# Include additional files?
	if($wgRtConfigSettings->Files) {
		# Loop through the array and specify each file on the command line
		foreach($wgRtConfigSettings->Files as $file) {
			$cmd .= "-f $file ";
		}
	}

	# Something in the source list?
	if($wgRtConfigSettings->SourceList) {
		# Pass it on...
		$cmd .= "-s $wgRtConfigSettings->SourceList ";
	}

	# Different format?
	if($wgRtConfigSettings->Format) {
		# Pass it on...
		$cmd .= "-config $wgRtConfigSettings->Format ";
	}

	# Different whois server?
	if($wgRtConfigSettings->WhoisHost) {
		# Pass it on...
		$cmd .= "-h $wgRtConfigSettings->WhoisHost ";
	}

	# Different whois port?
	if($wgRtConfigSettings->WhoisPort) {
		# Pass it on...
		$cmd .= "-p $wgRtConfigSettings->WhoisPort ";
	}

	# Different whois protocol?
	if($wgRtConfigSettings->WhoisProtocol) {
		# Pass it on...
		$cmd .= "-protocol $wgRtConfigSettings->WhoisProtocol ";
	}

	# Report Errors?
	if($wgRtConfigSettings->ReportErrors == true) {
		# Pass it on...
		$cmd .= "-report_errors ";
	} 

	# Ignore Errors?
	if($wgRtConfigSettings->IgnoreErrors == true) {
		# Pass it on...
		$cmd .= "-ignore_errors ";
	} 
	# Don't access-list match within route-maps?
	if($wgRtConfigSettings->NoMatchIPInbound == true) {
		# Pass it on...
		$cmd .= "-no_match_ip_inbound ";
	}

	# Don't cache access-lists?
	if($wgRtConfigSettings->DisableAccessListCache == true) {
		# Pass it on...
		$cmd .= "-disable_access_list_cache ";
	}

	# Supress Martians?
	if($wgRtConfigSettings->SupressMartians == true) {
		# Pass it on...
		$cmd .= "-supress_martian ";
	}

	# Don't compress access-lists?
	if($wgRtConfigSettings->CiscoNoCompressACLs == true) {
		# Pass it on...
		$cmd .= "-cisco_no_compress_acls ";
	}

	# Use prefix lists?
	if($wgRtConfigSettings->CiscoUsePrefixLists == true) {
		# Pass it on
		$cmd .= "-cisco_use_prefix_lists ";
	}

	# Eliminate Duplicate route-map entries?
	if($wgRtConfigSettings->CiscoEliminateDupMapParts == true) {
		# Pass it on
		$cmd .= "-cisco_eliminate_dup_map_parts ";
	}

	# Skip Route map generation?
	if($wgRtConfigSettings->CiscoSkipRouteMaps == true) {
		# Pass it on
		$cmd .= "-cisco_skip_route_maps ";
	}

	# Force Tilda in AS-Path?
	if($wgRtConfigSettings->CiscoForceTilda == true) {
		# Pass it on
		$cmd .= "-cisco_force_tilda ";
	}

	# Force interpreting ANY/NOT ANY?
	if($wgRtConfigSettings->CiscoEmptyLists == true) {
		# Pass it on
		$cmd .= "-cisco_empty_lists";
	}

	# Get the "safe" name of this page
	$pagename = urldecode($_GET['title']);
	$pagename = str_replace("&",'_amp_',$pagename);
	$pagename = str_replace("#",'_shrp_',$pagename);
        $pagename = str_replace("/",'_sd_',$pagename);
        $pagename = str_replace("\\",'_sd_',$pagename);

	# At this point we have finished building the command line...
	# Now we execute the program feeding it the input we got
	# and reading the output into our output variable and any
	# errors into the error variable

	# Prepare the file descriptiors
	$filedescspec = array(
		0 => array("pipe", "r"),	// stdin is a pipe
		1 => array("pipe", "w"),	// stdout is a pipe
		2 => array("pipe", "w")		// stderr is a pipe
	);

	# Launch the process
	$rtconfig = proc_open($cmd, $filedescspec, $pipes);

	# Check if we managed to launch the process...
	if (is_resource($rtconfig)) {
		# Write the input into stdin on the process
		fwrite($pipes[0],$timelinesrc);
		# Close the stdin
		fclose($pipes[0]);
		# Read what comes out the other end on stdout
		$output .= "<h3>RtConfig autogenerated configuration for $pagename</h3>";
		$output .= "<pre>";
		$output .= stream_get_contents($pipes[1]);
		$output .= "</pre>";
		# Close the stdout
		fclose($pipes[1]);
		# Read what come out of stderr
		$error .= stream_get_contents($pipes[2]);
		# Close the stderr
		fclose($pipes[2]);

		# Close the process
		$return_value = proc_close($rtconfig);

		# Check return value..
		if($return_value != 0) {
			$output .= "RtConfig returned an error, please double check the options that were passed";
			$output .= "to the RtConfig binary, the command line that was launched<pre>";
			$output .= $cmd;
			$output .= "</pre> and the numeric value that was returned was $return_value, RtConfig said: <pre>";
			$output .= $error;
			$output .= "</pre>";
		}
	} else {
		# We can't launch the binary, most likely explanation for that is that the path is wrong or that we're
		# running in safe mode
		$output .= "Failed to launch RtConfig, check whether the path<pre>";
		if($wgRtConfigSettings->Command) {
			$output .= " " . $wgRtConfigSettings->Command . " ";
		} else {
			$output .= " /usr/local/bin/RtConfig ";
		}
		$output .= "</pre> is valid and that you are not running PHP in safe mode, in which case<br/>";
		$output .= "this extension will not work<br/>";
	}
	# Output the command line and input and stderr of the Rtconfig binary if debug is set to true...
	if($wgRtConfigSettings->Debug == true) {
		$output .= "<h3>Debug</h3>";
		# Print command line
		$output .= "Command line: <pre>" . $cmd . "</pre><br>";
		# Print original input
		$output .= "Input: <pre>" . $timelinesrc . "</pre><br>";
		# Print stderr
		$output .= $error;
	}

	# Return the output back to MediaWiki
	return $output;
}
?>
