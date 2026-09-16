Pod::Spec.new do |s|
  s.name = 'SplatKitReactNative'
  s.version = '0.1.0-alpha.1'
  s.summary = 'Fabric view adapter for SplatKit native renderers'
  s.homepage = 'https://github.com/Xget7/react-native-splatkit'
  s.license = { :type => 'MIT' }
  s.author = { 'SplatKit' => 'opensource@splatkit.dev' }
  s.platforms = { :ios => '17.0' }
  s.source = { :git => 'https://github.com/Xget7/react-native-splatkit.git', :tag => s.version.to_s }
  s.source_files = 'ios/**/*.{h,mm}'
  s.public_header_files = 'ios/*.h'
  s.dependency 'React-Core'
  s.frameworks = 'UIKit', 'QuartzCore', 'Metal'
  s.requires_arc = true
  s.pod_target_xcconfig = {
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    'HEADER_SEARCH_PATHS' => '$(inherited) "${PODS_TARGET_SRCROOT}/../splatkit-ios/Sources/SplatKitCore/include"'
  }
end
